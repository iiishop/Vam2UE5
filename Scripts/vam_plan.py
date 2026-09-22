"""Stage 02: deterministic, read-only reference resolution and import planning.

No Unity/VaM code is executed and no geometry is decoded. Binary payloads are
streamed solely for SHA-256 identity. Saving a plan never changes import state.
"""
from __future__ import annotations
import collections
import contextlib
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import threading

VERSION = 1
CONFIG_EXTS = {'.vap', '.vam', '.vaj', '.vmi', '.json'}
PAYLOAD_EXTS = {'.vab', '.vmb', '.jpg', '.jpeg', '.png', '.tif', '.tiff', '.dds', '.tga', '.bmp'}
REFERENCE_EXTS = CONFIG_EXTS | PAYLOAD_EXTS | {'.cs', '.cslist', '.dll', '.assetbundle', '.obj', '.fbx', '.wav', '.mp3', '.ogg'}
ACTIONS = ('create', 'reuse', 'update', 'missing', 'unsupported')
MAX_NODES, MAX_EDGES, MAX_PACKAGES = 4096, 20000, 256
MAX_CONFIG = 8 * 1024 * 1024
MAX_DOCUMENTS = 64 * 1024 * 1024
MAX_FILE, MAX_TOTAL = 256 * 1024 * 1024, 2 * 1024 * 1024 * 1024
MAX_PLAN_STORE = 1024 * 1024 * 1024
VERSION_RE = re.compile(r'^(.+\..+)\.(\d+|latest|min\d+)$', re.I)


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def strict_json(data):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError('Duplicate JSON key: ' + key)
            out[key] = value
        return out
    def constant(value):
        raise ValueError('Invalid JSON numeric constant: ' + value)
    obj = json.loads(data.decode('utf-8-sig'), object_pairs_hook=pairs, parse_constant=constant)
    if not isinstance(obj, dict):
        raise ValueError('Configuration must be a JSON object')
    # Also rejects overflow literals such as 1e999 and isolated Unicode surrogates.
    canonical(obj)
    return obj


def pointer(parts):
    return '/' + '/'.join(str(p).replace('~', '~0').replace('/', '~1') for p in parts)


def is_hair_credit_trailer(text):
    quoted = r'"(?:[^"\\\x00-\x1f]|\\["\\/bfnrt]|\\u[0-9a-fA-F]{4})*"'
    return len(text) <= 4096 and re.fullmatch(r'Thanks:\s*' + quoted + r'\s*\+\s*"hair_version: [^"\\\x00-\x1f]+"\s*', text) is not None


def resource_json(data, path):
    """Narrow compatibility for observed hair .vam credit trailers; never relax JSON itself."""
    try:
        return strict_json(data), []
    except json.JSONDecodeError as error:
        suffix = PurePosixPath(path).suffix.lower()
        hair_config = suffix == '.vaj' and path.replace('\\', '/').casefold().startswith('custom/hair/')
        if error.msg != 'Extra data' or not (suffix == '.vam' or hair_config):
            raise
        text = data.decode('utf-8-sig')
        trailer = text[error.pos:]
        # Only the observed two quoted credit/version fields are admissible.
        if not is_hair_credit_trailer(trailer):
            raise
        obj = strict_json(text[:error.pos].encode('utf-8'))
        if suffix == '.vam' and obj.get('itemType') not in ('HairFemale', 'HairMale'):
            raise
        if hair_config and not (isinstance(obj.get('components'), list) and isinstance(obj.get('storables'), list)):
            raise
        return obj, [{'code':'vam_credit_trailer', 'path':path, 'character_offset':error.pos,
                      'raw_trailer':trailer, 'message':'Recognized non-JSON hair credits retained outside parameters'}]


def norm_path(path, base=''):
    path = path.replace('\\', '/')
    if path.startswith('/') or ':' in path or '\x00' in path:
        raise ValueError('Absolute or unsafe resource path')
    parts = []
    for p in (base + '/' + path).split('/'):
        if p in ('', '.'):
            continue
        if p == '..':
            if not parts:
                raise ValueError('Relative reference escapes source root')
            parts.pop()
        else:
            parts.append(p)
    return '/'.join(parts)


class PlanFailure(Exception):
    def __init__(self, action, code, message):
        super().__init__(message)
        self.action, self.code = action, code


class Planner:
    def __init__(self, catalog, baseline=None, locked=None, cancel=None, progress=None):
        self.cat = catalog
        with catalog.connect() as db:
            root = db.execute("SELECT value FROM meta WHERE key='root'").fetchone()
            self.package_sources = [r[0] for r in db.execute("SELECT key FROM sources WHERE key NOT LIKE 'loose:%' ORDER BY key")]
        if not root:
            raise ValueError('请先完成资源索引。')
        self.root = Path(root[0]).resolve()
        self.baseline = baseline or {'schema': 1, 'assets': {}}
        self.locked = locked
        self.cancel = cancel or threading.Event()
        self.progress = progress or (lambda **_: None)
        self.items, self.documents, self.snapshots = {}, {}, {}
        self.edges, self.inactive, self.cycles, self.errors = [], [], [], []
        self.visiting = []
        self.package_map = collections.defaultdict(list)
        self.families = collections.defaultdict(dict)
        for source in self.package_sources:
            package = PurePosixPath(source).stem
            self.package_map[package.casefold()].append(source)
            match = VERSION_RE.fullmatch(package)
            if match and match[2].isdigit():
                self.families[match[1].casefold()][int(match[2])] = package
        self.zips = collections.OrderedDict()
        self.package_metadata, self.version_locks = {}, {}
        self.source_stamps, self.package_members = {}, {}
        self.bytes_read = self.document_bytes = 0
        self.old_locks = {x['requested'].casefold(): x for x in (locked or {}).get('version_locks', [])}
        self.builtin_catalog = None
        self.builtin_files = {}

    def builtin_candidates(self, role, name, gender=''):
        if self.builtin_catalog is None:
            catalog_path = Path(__file__).parents[1] / 'Config/BuiltinCatalog.json'
            self.builtin_catalog = strict_json(catalog_path.read_bytes()) if catalog_path.exists() else {'schema': 1, 'entries': [], 'files': {}}
            if self.builtin_catalog.get('schema') != 1:
                raise ValueError('Unsupported builtin catalog schema')
        matches = [e for e in self.builtin_catalog['entries'] if e['role'] == role and name in e['names']
                   and (not gender or e['gender'] in (gender, 'any'))]
        # Duplicate registrations of the same physical target are one resource.
        unique = {}
        for entry in matches:
            unique.setdefault(canonical(entry['locator']), entry)
        return list(unique.values())

    def builtin(self, role, raw, gender):
        matches = self.builtin_candidates(role, raw, gender if role != 'character' else '')
        if not matches:
            raise PlanFailure('unsupported', 'builtin_adapter_required', f'未确认的内置标识：{role}/{gender or "未知性别"}/{raw}')
        if len(matches) != 1:
            raise PlanFailure('unsupported', 'builtin_ambiguous', f'内置标识有 {len(matches)} 个不同来源，需要明确人物性别或资源：{role}/{raw}')
        entry = matches[0]
        locked_files = {}
        for name in entry['files']:
            expected = self.builtin_catalog['files'][name]
            path = expected['path']
            if name not in self.builtin_files:
                p = self.source_path(path)
                if not p.is_file():
                    raise PlanFailure('missing', 'builtin_file_missing', '内置来源缺失：' + path)
                self.stamp(p)
                # Bundles may contain large material data. Hash streams only; no decoding.
                digest, size = hashlib.sha256(), 0
                with p.open('rb') as stream:
                    while True:
                        self.check()
                        chunk = stream.read(1024 * 1024)
                        if not chunk: break
                        size += len(chunk)
                        self.bytes_read += len(chunk)
                        if self.bytes_read > MAX_TOTAL:
                            raise PlanFailure('unsupported', 'read_limit', '内置来源校验超过计划读取预算')
                        digest.update(chunk)
                self.stamp(p)
                if size != expected['size'] or digest.hexdigest() != expected['sha256']:
                    raise PlanFailure('unsupported', 'builtin_catalog_stale', '内置来源与映射版本不符，请重新生成映射：' + path)
                self.builtin_files[name] = expected
            locked_files[name] = self.builtin_files[name]
        mapping = {'entry': entry, 'files': locked_files}
        identity = sha(canonical(['builtin', role, entry['locator']]))
        self.items.setdefault(identity, {'id': identity, 'source': 'builtin', 'path': raw,
            'resource_kind': role, 'sha256': sha(canonical(mapping)), 'action': 'create',
            'code': 'builtin_mapped', 'reason': '已映射内置来源；此阶段仅生成计划，尚未转换为 UE 资产。',
            'builtin_mapping': mapping, 'references': []})
        return identity

    def check(self):
        if self.cancel.is_set():
            raise PlanFailure('unsupported', 'cancelled', '计划生成已取消')
        if len(self.items) >= MAX_NODES or len(self.edges) >= MAX_EDGES:
            raise PlanFailure('unsupported', 'budget_exceeded', 'Dependency graph exceeds configured node / edge limit')

    def stamp(self, path):
        st = path.stat()
        key = str(path)
        value = (st.st_size, st.st_mtime_ns)
        if key in self.source_stamps and self.source_stamps[key] != value:
            raise PlanFailure('unsupported', 'source_changed', 'Source changed during plan generation: ' + key)
        self.source_stamps[key] = value
        return st

    def source_path(self, relative):
        p = (self.root / norm_path(relative)).resolve()
        if not p.is_relative_to(self.root):
            raise PlanFailure('unsupported', 'outside_root', 'Reference escapes VaM root: ' + relative)
        return p

    def archive(self, source):
        self.check()
        self.stamp(self.source_path(source))
        if source in self.zips:
            self.zips.move_to_end(source)
            return self.zips[source]
        if len(self.package_metadata) >= MAX_PACKAGES and source not in self.package_metadata:
            raise PlanFailure('unsupported', 'package_limit', 'Too many packages in one plan')
        while len(self.zips) >= 4:
            _, old = self.zips.popitem(last=False)
            old[0].close()
        z = self.cat.open_zip(self.source_path(source))
        names = {}
        for info in z.infolist():
            if info.is_dir():
                continue
            try:
                name = norm_path(info.filename)
            except ValueError:
                continue
            key = name.casefold()
            # Never silently choose one of two conflicting ZIP members.
            names[key] = None if key in names else info
        self.zips[source] = (z, names)
        return z, names

    def locate(self, source, path):
        path = norm_path(path)
        if source:
            _, names = self.archive(source)
            if path.casefold() not in names:
                raise PlanFailure('missing', 'file_missing', 'Package member missing: ' + path)
            info = names[path.casefold()]
            if info is None:
                raise PlanFailure('unsupported', 'ambiguous_member', 'Duplicate package member: ' + path)
            return norm_path(info.filename)
        p = self.source_path(path)
        if not p.is_file():
            raise PlanFailure('missing', 'file_missing', 'Loose resource missing: ' + path)
        return p.relative_to(self.root).as_posix()

    def choose_package(self, requested):
        key = requested.casefold()
        if key in self.version_locks:
            return self.version_locks[key]['source']
        match = VERSION_RE.fullmatch(requested)
        if not match:
            raise PlanFailure('unsupported', 'version_syntax', 'Unsupported package version syntax: ' + requested)
        family, selector = match[1], match[2].lower()
        old = self.old_locks.get(key)
        if self.locked is not None and not old:
            raise PlanFailure('unsupported', 'lock_incomplete', 'Reference is absent from saved lock: ' + requested)
        if old:
            resolved = old['resolved']
        elif selector.isdigit():
            resolved = requested
        else:
            minimum = int(selector[3:]) if selector.startswith('min') else 0
            choices = self.families.get(family.casefold(), {})
            eligible = [n for n in choices if n >= minimum]
            if not eligible:
                raise PlanFailure('missing', 'version_missing', 'No installed version satisfies ' + requested)
            resolved = choices[max(eligible)]
        sources = self.package_map.get(resolved.casefold(), [])
        if not sources:
            available = sorted(self.families.get(family.casefold(), {}))
            raise PlanFailure('missing', 'version_missing', f'Package version missing: {resolved}; installed: {available}')
        if len(sources) != 1:
            raise PlanFailure('unsupported', 'ambiguous_package', 'Multiple files claim the same package ID: ' + resolved + ' :: ' + ' | '.join(sources))
        source = sources[0]
        if old and (old['source'] != source or not resolved.casefold().startswith(family.casefold() + '.')):
            raise PlanFailure('unsupported', 'lock_mismatch', 'Locked package location differs: ' + requested)
        actual = PurePosixPath(source).stem
        self.version_locks[key] = {'requested': requested, 'resolved': actual, 'source': source,
                                   'policy': 'exact' if selector.isdigit() else 'highest_installed_satisfying_bound'}
        return source

    def resolve(self, raw, source, path):
        text = raw.replace('\\', '/')
        if re.match(r'^[A-Za-z]:/', text):
            raise PlanFailure('unsupported', 'absolute_path', 'Absolute OS path is not a portable VaM reference')
        if re.match(r'^[a-z]+://', text, re.I):
            raise PlanFailure('unsupported', 'external_url', 'External URLs are not fetched during planning')
        if ':/' in text:
            package, member = text.split(':/', 1)
            if package.casefold() == 'self':
                if not source:
                    raise PlanFailure('missing', 'self_without_package', 'SELF has no package context in a loose preset')
                target_source = source
            else:
                target_source = self.choose_package(package)
            return target_source, self.locate(target_source, member)
        rooted = text.casefold().startswith(('custom/', 'saves/', 'assets/'))
        target = norm_path(text, '' if rooted else str(PurePosixPath(path).parent))
        if rooted and source:
            # Explicit project paths: local package first, then loose installation.
            try:
                return source, self.locate(source, target)
            except PlanFailure as error:
                if error.code != 'file_missing':
                    raise
                return '', self.locate('', target)
        return source, self.locate(source, target)

    def read(self, source, path, config):
        limit = MAX_CONFIG if config else MAX_FILE
        if source:
            z, names = self.archive(source)
            info = names[path.casefold()]
            size = info.file_size
            stream = lambda: z.open(info)
        else:
            p = self.source_path(path)
            size = self.stamp(p).st_size
            stream = lambda: p.open('rb')
        if size > limit or self.bytes_read + size > MAX_TOTAL:
            raise PlanFailure('unsupported', 'read_limit', 'Resource exceeds hashing / metadata budget: ' + path)
        if config and self.document_bytes + size > MAX_DOCUMENTS:
            raise PlanFailure('unsupported', 'document_limit', 'Configuration snapshots exceed 64 MiB')
        h, chunks, total = hashlib.sha256(), [], 0
        with stream() as f:
            while True:
                self.check()
                chunk = f.read(min(1024 * 1024, limit - total + 1))
                if not chunk:
                    break
                total += len(chunk)
                if total > limit or self.bytes_read + len(chunk) > MAX_TOTAL:
                    raise PlanFailure('unsupported', 'read_limit', 'Resource grew beyond read budget')
                self.bytes_read += len(chunk)
                h.update(chunk)
                if config:
                    chunks.append(chunk)
        if total != size:
            raise PlanFailure('unsupported', 'source_changed', 'Resource size changed while hashing: ' + path)
        self.stamp(self.source_path(source or path))
        if config:
            self.document_bytes += total
        return h.hexdigest(), total, b''.join(chunks) if config else None

    @staticmethod
    def identity(source, path):
        return sha((source.casefold() + '\0' + path.casefold()).encode('utf-8'))

    def problem(self, origin, field, raw, error, target_key=None):
        code = getattr(error, 'code', 'invalid_configuration')
        action = getattr(error, 'action', 'unsupported')
        identity = sha(canonical([target_key, code] if target_key else [origin, field, raw, code]))
        self.items.setdefault(identity, {'id': identity, 'source': '', 'path': raw,
            'action': action, 'code': code, 'reason': str(error), 'resource_kind': 'unresolved',
            'references': [], 'sha256': None})
        return identity

    def edge(self, origin, field, raw, target, active=True, reason=''):
        origin_item = self.items.get(origin)
        location = ((origin_item['source'] + ':/' if origin_item['source'] else '') + origin_item['path']) if origin_item else origin
        record = {'from': origin, 'source_location': location, 'field': field, 'reference': raw, 'to': target}
        if not active:
            record['reason'] = reason
            self.inactive.append(record)
        else:
            self.edges.append(record)
        return record

    def reference(self, raw, source, path, origin, field, role='file', active=True, known=True, gender=''):
        self.check()
        if not active:
            self.edge(origin, field, raw, '', False, 'disabled_or_zero_weight')
            return
        # A locked partial plan also locks its exclusions. Unresolved references
        # never acquired a package version lock; resolving them again would turn
        # their original diagnostic into lock_incomplete and change plan identity.
        if self.locked:
            prior = next((e for e in self.locked.get('edges', [])
                          if e['from'] == origin and e['field'] == field and e['reference'] == raw), None)
            saved = next((i for i in self.locked.get('items', [])
                          if prior and i['id'] == prior['to']), None)
            if (saved and saved.get('resource_kind') == 'unresolved'
                    and saved.get('action') in ('missing', 'unsupported') and not saved.get('sha256')):
                self.items.setdefault(saved['id'], dict(saved, references=[]))
                self.edge(origin, field, raw, saved['id'])
                return
        try:
            if not known:
                raise PlanFailure('unsupported', 'uninterpreted_reference', 'Reference-like value in an unknown field; preserved for review')
            if role != 'file' and not self.looks_file(raw):
                identity = self.builtin(role, raw, gender)
                self.edge(origin, field, raw, identity)
                return
            target_source, target_path = self.resolve(raw, source, path)
            identity = self.identity(target_source, target_path)
            self.edge(origin, field, raw, identity)
            self.visit(target_source, target_path)
        except Exception as error:
            if isinstance(error, PlanFailure) and error.code in ('cancelled', 'budget_exceeded'):
                raise
            context = None
            if getattr(error, 'action', '') == 'missing':
                normalized = raw.replace('\\', '/')
                if ':/' in normalized and not normalized.lower().startswith('self:/'):
                    context = normalized.casefold()
                else:
                    context = source.casefold() + ':/' + str(PurePosixPath(path).parent).casefold() + '/' + normalized.casefold()
            identity = self.problem(origin, field, raw, error, context)
            self.edge(origin, field, raw, identity)

    @staticmethod
    def looks_file(value):
        normalized = value.replace('\\', '/').casefold()
        return (':/' in normalized or normalized.startswith(('custom/', 'saves/', 'assets/', './', '../')) or
                PurePosixPath(value).suffix.lower() in REFERENCE_EXTS)

    def walk_refs(self, value, source, path, origin, parts=(), active=True, depth=0, gender=''):
        if depth > 64:
            raise ValueError('Configuration nesting exceeds 64 levels')
        if isinstance(value, dict):
            character = value.get('character')
            if not character and isinstance(value.get('storables'), list):
                character = next((s.get('character') for s in value['storables'] if isinstance(s, dict) and s.get('id') == 'geometry'), None)
            if isinstance(character, str):
                candidates = self.builtin_candidates('character', character)
                gender = candidates[0]['gender'] if len(candidates) == 1 else ''
            if 'enabled' in value and str(value['enabled']).lower() == 'false':
                active = False
            group = str(parts[-2]).lower() if len(parts) >= 2 else ''
            if group.startswith('morph') and 'value' in value:
                try:
                    weight = float(value['value'])
                    if not math.isfinite(weight): raise ValueError('Non-finite morph weight')
                    if weight == 0: active = False
                except (ValueError, TypeError):
                    raise ValueError('Invalid morph weight at ' + pointer(parts))
            for key in sorted(value):
                x = value[key]
                p = parts + (key,)
                typed_arrays = set()
                if not parts or (len(parts) >= 2 and parts[-2] == 'atoms'):
                    typed_arrays.update(('storables', 'atoms'))
                if value.get('id') == 'geometry':
                    typed_arrays.update(('clothing', 'hair', 'morphs', 'morphsOtherGender'))
                if key in typed_arrays and not isinstance(x, list):
                    raise ValueError('Expected array at ' + pointer(p))
                if key in typed_arrays and any(not isinstance(entry, dict) for entry in x):
                    raise ValueError('Expected object entries at ' + pointer(p))
                if isinstance(x, str) and x and x.casefold() not in ('null', 'none'):
                    k = key.lower()
                    role = 'file'
                    if k == 'character': role = 'character'
                    elif k == 'id' and group in ('clothing', 'hair'): role = group
                    elif k in ('uid', 'name') and group.startswith('morph'):
                        if k == 'name' and value.get('uid'): continue
                        role = 'morph'
                    elif k in ('id', 'uid', 'internalid', 'displayname', 'name', 'description', 'credits', 'instructions', 'promotionalLink'.lower()):
                        continue
                    known = (role != 'file' or k.startswith('customtexture_') or
                             k in ('simtexture', 'path', 'filepath', 'file', 'texture', 'url', 'ref', 'include',
                                   'preset', 'presetpath', 'asseturl', 'assetbundleurl', 'subscenepath') or
                             k.endswith('url') or (len(parts) and str(parts[-1]).lower() == 'plugins'))
                    if role != 'file' or self.looks_file(x) or known:
                        # Unknown ordinary text is retained, not interpreted as a builtin.
                        ref_gender = {'male': 'female', 'female': 'male'}.get(gender, '') if group == 'morphsothergender' else gender
                        self.reference(x, source, path, origin, pointer(p), role, active, known, ref_gender)
                elif isinstance(x, (dict, list)):
                    self.walk_refs(x, source, path, origin, p, active, depth + 1, gender)
        elif isinstance(value, list):
            for i, x in enumerate(value):
                if isinstance(x, (dict, list)):
                    self.walk_refs(x, source, path, origin, parts + (i,), active, depth + 1, gender)
                elif isinstance(x, str) and self.looks_file(x):
                    self.reference(x, source, path, origin, pointer(parts + (i,)), active=active,
                                   known=bool(parts and str(parts[-1]).lower() in ('includes', 'references', 'files')))

    def package_meta(self, source):
        if not source or source in self.package_metadata:
            return
        self.package_metadata[source] = {'source': source, 'package': PurePosixPath(source).stem, 'declarations': []}
        try:
            path = self.locate(source, 'meta.json')
            raw_hash, size, raw = self.read(source, path, True)
            self.snapshots[raw_hash] = raw
            self.package_metadata[source]['sha256'] = raw_hash
            obj = strict_json(raw)
            deps = obj.get('dependencies', {})
            if not isinstance(deps, dict):
                raise ValueError('meta.json dependencies must be an object')
            self.package_metadata[source].update(sha256=raw_hash, parameters=obj)
            def declared(mapping, chain=()):
                for requested in sorted(mapping):
                    if sum(len(m['declarations']) for m in self.package_metadata.values()) >= MAX_EDGES:
                        raise ValueError('Declared dependency graph exceeds 20,000 entries')
                    data = mapping[requested]
                    if not isinstance(data, dict):
                        raise ValueError('Dependency declaration must be an object: ' + requested)
                    self.package_metadata[source]['declarations'].append({'requested': requested, 'chain': list(chain)})
                    child = data.get('dependencies', {})
                    if not isinstance(child, dict):
                        raise ValueError('Nested dependencies must be an object')
                    if len(chain) >= 32:
                        raise ValueError('Declared dependency nesting exceeds 32')
                    declared(child, chain + (requested,))
            declared(deps)
        except Exception as error:
            origin = source + ':/meta.json'
            self.package_metadata[source]['parse_error'] = str(error)
            target = self.problem(origin, '/dependencies', source + ':/meta.json', error)
            self.edge(origin, '/dependencies', source + ':/meta.json', target)

    def visit(self, source, path):
        self.check()
        if len(self.visiting) >= 128:
            raise PlanFailure('unsupported', 'graph_depth', 'Reference traversal exceeds 128 levels')
        identity = self.identity(source, path)
        if identity in self.visiting:
            self.cycles.append(self.visiting[self.visiting.index(identity):] + [identity])
            return identity
        if identity in self.items:
            return identity
        self.items[identity] = {'id': identity, 'source': source, 'path': path,
                                'resource_kind': PurePosixPath(path).suffix.lower().lstrip('.'),
                                'action': 'create', 'references': [], 'sha256': None}
        item = self.items[identity]
        self.visiting.append(identity)
        self.progress(done=len(self.items), phase=(PurePosixPath(source).stem + ':/' if source else '') + path)
        try:
            self.package_meta(source)
            ext = PurePosixPath(path).suffix.lower()
            if ext not in CONFIG_EXTS | PAYLOAD_EXTS:
                raise PlanFailure('unsupported', 'format_unsupported', 'No import-plan adapter for ' + ext + '; source code is never executed')
            h, size, raw = self.read(source, path, ext in CONFIG_EXTS)
            item.update(sha256=h, size=size)
            if raw is not None:
                # Retain raw bytes even when strict parsing fails.
                self.snapshots[h] = raw
                obj, compatibility = resource_json(raw, path)
                self.documents[identity] = {'id': identity, 'raw_sha256': h, 'parameters': obj}
                if compatibility:
                    self.documents[identity]['compatibility_warnings'] = compatibility
                    item['warnings'] = compatibility
                self.walk_refs(obj, source, path, identity)
                companions = []
                if ext == '.vam' and path.casefold().startswith(('custom/clothing/', 'custom/hair/')):
                    companions = ['.vaj', '.vab']
                elif ext == '.vmi' and str(obj.get('numDeltas', '1')) != '0':
                    companions = ['.vmb']
                for suffix in companions:
                    target = PurePosixPath(path).with_suffix(suffix).name
                    self.reference(target, source, path, identity, '/$companion/' + suffix)
        except Exception as error:
            if isinstance(error, PlanFailure) and error.code == 'cancelled':
                raise
            item.update(action=getattr(error, 'action', 'unsupported'), code=getattr(error, 'code', 'invalid_configuration'), reason=str(error))
        finally:
            self.visiting.pop()
        return identity

    def baseline_actions(self):
        if self.baseline.get('schema') != 1 or not isinstance(self.baseline.get('assets'), dict):
            raise ValueError('Import-state manifest must have schema=1 and assets object')
        existing = self.baseline['assets']
        hashes = {}
        for key in sorted(self.items):
            item = self.items[key]
            if item['action'] not in ('create', 'reuse', 'update') or not item['sha256']:
                continue
            old = existing.get(key)
            if old is not None:
                if not isinstance(old, dict) or not re.fullmatch('[a-f0-9]{64}', str(old.get('sha256', ''))):
                    raise ValueError('Invalid imported asset fingerprint for ' + key)
                item['action'] = 'reuse' if old['sha256'] == item['sha256'] else 'update'
                item['basis'] = 'import_state'
                if 'destination' in old: item['destination'] = old['destination']
            else:
                # Configuration bytes have context-dependent relative references; do not content-dedup them.
                content = (item['resource_kind'], item['sha256'])
                if '.' + item['resource_kind'] not in CONFIG_EXTS and content in hashes:
                    item.update(action='reuse', basis='same_payload_in_plan', reuse_of=hashes[content])
                else:
                    item['basis'] = 'new_source'
            if '.' + item['resource_kind'] not in CONFIG_EXTS:
                hashes.setdefault((item['resource_kind'], item['sha256']), key)

    def declarations(self):
        used = {item['source'] for item in self.items.values() if item['source'] not in ('', 'builtin')}
        graph = collections.defaultdict(set)
        references_by_family = collections.defaultdict(list)
        for edge in self.edges:
            a, b = self.items.get(edge['from']), self.items.get(edge['to'])
            if a and b and b['source'] in used and a['source'] != b['source']:
                graph[a['source']].add(b['source'])
            raw = edge['reference'].replace('\\', '/')
            if a and ':/' in raw:
                request = raw.split(':/', 1)[0]
                match = VERSION_RE.fullmatch(request)
                if match:
                    references_by_family[match[1].casefold()].append((edge, a['source'], request, match[2].lower()))
        results = []
        for source in sorted(self.package_metadata):
            metadata = self.package_metadata[source]
            reachable, queue = set(), list(graph[source])
            while queue:
                current = queue.pop()
                if current in reachable: continue
                reachable.add(current)
                queue.extend(graph[current] - reachable)
            for declaration in metadata['declarations']:
                requested = declaration['requested']
                match = VERSION_RE.fullmatch(requested)
                compatible = []
                available = []
                actual_references = []
                if match:
                    family, selector = match[1].casefold(), match[2].lower()
                    for version, package in sorted(self.families.get(family, {}).items()):
                        if selector.isdigit() and version != int(selector): continue
                        if selector.startswith('min') and version < int(selector[3:]): continue
                        available.append(package)
                        compatible.extend(p for p in self.package_map[package.casefold()] if p in reachable)
                    for edge, origin_source, request, requested_version in references_by_family[family]:
                        if origin_source not in reachable and origin_source != source: continue
                        lock = self.version_locks.get(request.casefold())
                        actual_version = lock['resolved'].rsplit('.', 1)[-1] if lock else requested_version
                        if actual_version.isdigit():
                            if selector.isdigit() and int(selector) != int(actual_version): continue
                            if selector.startswith('min') and int(actual_version) < int(selector[3:]): continue
                        actual_references.append({k: edge[k] for k in ('source_location', 'field', 'reference')})
                        # This is a declaration summary; complete provenance remains in plan.edges.
                        if len(actual_references) >= 30: break
                results.append({'declared_by': source, **declaration,
                                'used': bool(compatible or actual_references), 'used_sources': sorted(compatible),
                                'references': sorted(actual_references, key=canonical),
                                'availability': 'installed' if available else 'missing_or_unknown',
                                'blocking': False})
        return results

    def generate(self, selection):
        selected_ids = sorted(set(selection))
        if not selected_ids or len(selected_ids) > 32:
            raise ValueError('Select between 1 and 32 catalog resources')
        roots = []
        try:
            for selected in selected_ids:
                self.check()
                try:
                    asset, root = self.cat.asset(selected)
                    if root.resolve() != self.root:
                        raise ValueError('Catalog source root changed')
                    source = '' if asset['source'].startswith('loose:') else asset['source']
                    path = self.locate(source, asset['path'])
                    roots.append(self.visit(source, path))
                except Exception as error:
                    if isinstance(error, PlanFailure) and error.code == 'cancelled': raise
                    target = self.problem('selection:' + selected, '/selection', selected, error)
                    self.edge('selection:' + selected, '/selection', selected, target)
            self.baseline_actions()
            # Verify all files and archives stayed unchanged for the entire read window.
            for filename in sorted(self.source_stamps): self.stamp(Path(filename))
            if self.locked:
                if self.locked.get('baseline_sha256') != sha(canonical(self.baseline)):
                    raise PlanFailure('unsupported', 'locked_baseline_changed', 'Import-state baseline differs from saved plan')
                expected = {r['id']: r.get('sha256') for r in self.locked.get('items', []) if r.get('sha256')}
                current = {r['id']: r.get('sha256') for r in self.items.values() if r.get('sha256')}
                if expected != current:
                    raise PlanFailure('unsupported', 'locked_content_changed', 'Saved source fingerprints differ; locked plan cannot be reproduced')
                if self.locked.get('selection') != selected_ids:
                    raise PlanFailure('unsupported', 'locked_selection_changed', 'Selected resources differ from saved plan')
                old_meta = {k: v.get('sha256') for k, v in self.locked.get('packages', {}).items()}
                new_meta = {k: v.get('sha256') for k, v in self.package_metadata.items()}
                if old_meta != new_meta:
                    raise PlanFailure('unsupported', 'locked_metadata_changed', 'Locked package metadata has changed')
        except Exception as error:
            target = self.problem('plan', '/', 'plan', error)
            self.edge('plan', '/', 'plan', target)
        finally:
            for z, _ in self.zips.values(): z.close()
            self.zips.clear()
        for edge in self.edges:
            item = self.items.get(edge['to'])
            if item is not None:
                item['references'].append({k: edge[k] for k in ('from', 'source_location', 'field', 'reference')})
        for item in self.items.values():
            item['references'] = sorted(item['references'], key=canonical)
        items = sorted(self.items.values(), key=lambda x: x['id'])
        counts = {a: sum(i['action'] == a for i in items) for a in ACTIONS}
        cancelled = any(i.get('code') == 'cancelled' for i in items)
        status = 'cancelled' if cancelled else ('blocked' if counts['missing'] or counts['unsupported'] else 'ready')
        plan = {'schema': VERSION, 'resolver': 'vam-plan-2', 'status': status,
                'selection': selected_ids, 'roots': sorted(set(roots)), 'source_root': str(self.root),
                'baseline_sha256': sha(canonical(self.baseline)), 'counts': counts, 'items': items,
                'edges': sorted(self.edges, key=canonical), 'inactive_references': sorted(self.inactive, key=canonical),
                'cycles': sorted(self.cycles),
                'declared_dependencies': self.locked['declared_dependencies'] if self.locked else self.declarations(),
                'version_locks': sorted(self.version_locks.values(), key=lambda x: x['requested'].casefold()),
                'packages': self.package_metadata, 'documents': self.documents,
                'preservation': 'All parsed values and unknown fields retained verbatim in parameters; exact configuration bytes saved by SHA-256.',
                'policies': {'latest': 'highest_installed_numeric_version', 'minN': 'highest_installed_version_at_least_N',
                             'builtins': 'verified_source_mapping_with_content_locks', 'cycles': 'retained_as_graph_edges_not_recursively_expanded',
                             'inactive': 'disabled_entries_and_zero_weight_morphs_do_not_create_dependencies',
                             'missing_declarations': 'nonblocking_until_referenced', 'config_dedup': 'source_identity_only'}}
        plan['plan_id'] = sha(canonical(plan))
        return plan


class PlanService:
    def __init__(self, catalog):
        self.catalog = catalog
        self.directory = catalog.data / 'Plans'
        self.directory.mkdir(exist_ok=True)
        self.mutex = threading.RLock()
        self.cancel = threading.Event()
        self.state = {'running': False, 'done': 0, 'phase': '', 'plan_id': '', 'error': ''}
        self.cached = None

    def read_plan(self, identity):
        if not re.fullmatch('[a-f0-9]{64}', identity): raise ValueError('Invalid plan ID')
        path = self.directory / (identity + '.json')
        stat = path.stat()
        if stat.st_size > 256 * 1024 * 1024: raise ValueError('Saved plan exceeds size limit')
        key = (identity, stat.st_size, stat.st_mtime_ns)
        with self.mutex:
            if self.cached and self.cached[0] == key:
                return self.cached[1]
        data = path.read_bytes()
        if len(data) > 256 * 1024 * 1024: raise ValueError('Saved plan exceeds size limit')
        plan = strict_json(data)
        supplied = plan.pop('plan_id', None)
        if supplied != identity or sha(canonical(plan)) != identity: raise ValueError('Saved plan content hash is invalid')
        plan['plan_id'] = supplied
        with self.mutex: self.cached = (key, plan)
        return plan

    def status(self):
        with self.mutex: return dict(self.state)

    def history(self):
        files = sorted(self.directory.glob('*.json'), key=lambda p: (p.stat().st_mtime_ns, p.name), reverse=True)[:50]
        return {'items': [{'id': p.stem, 'bytes': p.stat().st_size, 'modified': p.stat().st_mtime} for p in files]}

    def progress(self, **values):
        with self.mutex: self.state.update(values)

    def start(self, selection, locked_id=''):
        if not isinstance(selection, list) or not 1 <= len(selection) <= 32 or any(not isinstance(x, str) for x in selection):
            raise ValueError('Choose 1–32 indexed resources')
        with self.mutex:
            if self.state['running']: raise ValueError('已有计划正在生成，请等待或取消。')
            self.cancel.clear()
            self.state = {'running': True, 'done': 0, 'phase': '解析预设与依赖', 'plan_id': '', 'error': ''}
            threading.Thread(target=self.run, args=(selection, locked_id), daemon=True, name='VaM-Plan').start()
        return self.status()

    def run(self, selection, locked_id):
        try:
            baseline_file = self.catalog.data / 'ImportState' / 'manifest.json'
            if baseline_file.exists() and baseline_file.stat().st_size > MAX_CONFIG:
                raise ValueError('Import-state manifest exceeds 8 MiB')
            baseline = strict_json(baseline_file.read_bytes()) if baseline_file.exists() else None
            locked = self.read_plan(locked_id) if locked_id else None
            planner = Planner(self.catalog, baseline=baseline, locked=locked, cancel=self.cancel, progress=self.progress)
            plan = planner.generate(selection)
            payload = canonical(plan)
            objects = self.directory / 'Objects'
            objects.mkdir(exist_ok=True)
            # Plans are records, not an LRU cache: refuse excess storage rather than deleting user plans.
            used = sum(p.stat().st_size for p in self.directory.rglob('*') if p.is_file())
            needed = len(payload) + sum(len(v) for k, v in planner.snapshots.items() if not (objects / k).exists())
            target = self.directory / (plan['plan_id'] + '.json')
            if used + (0 if target.exists() else needed) > MAX_PLAN_STORE:
                raise ValueError('计划存储超过 1 GiB 上限；请备份并清理旧计划后重试。')
            for h, raw in planner.snapshots.items():
                output = objects / h
                if not output.exists():
                    tmp = output.with_suffix('.tmp')
                    tmp.write_bytes(raw)
                    tmp.replace(output)
            tmp = target.with_suffix('.tmp')
            tmp.write_bytes(payload)
            tmp.replace(target)
            self.progress(plan_id=plan['plan_id'], status=plan['status'], counts=plan['counts'],
                          file=str(target), phase='计划已保存；未构建或导入资产')
        except Exception as error:
            self.progress(error=str(error), status='failed', phase='计划失败')
        finally:
            self.progress(running=False)

    def result(self, identity, action='', offset=0):
        plan = self.read_plan(identity)
        filtered = [i for i in plan['items'] if not action or i['action'] == action]
        offset = max(0, int(offset))
        # Full raw parameters stay on disk; the GUI only receives one page of items.
        return {'plan_id': identity, 'status': plan['status'], 'counts': plan['counts'],
                'file': str(self.directory / (identity + '.json')), 'items': filtered[offset:offset+100],
                'total': len(filtered), 'offset': offset, 'version_locks': plan['version_locks'],
                'declared_dependencies': plan['declared_dependencies'], 'cycles': plan['cycles'],
                'inactive_count': len(plan['inactive_references']), 'selection': plan['selection']}
