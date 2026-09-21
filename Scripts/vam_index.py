"""Read-only VaM catalog. Standard library only; runs in UE's bundled Python.

Only .vam/.vmi and package metadata are read during indexing. Preset bodies are
read on selection; .vab/.vmb/.vaj/OBJ and other geometry are never decoded.
"""
from __future__ import annotations
import argparse
import contextlib
import ctypes
import datetime as dt
import hashlib
import http.server
import json
import logging
import os
from pathlib import Path, PurePosixPath
import secrets
import sqlite3
import struct
import threading
import time
import urllib.parse
import zipfile

SCHEMA = 1
CLASSIFIER_VERSION = '2'
META_LIMIT = 256 * 1024
DETAIL_LIMIT = 2 * 1024 * 1024
IMAGE_LIMIT = 8 * 1024 * 1024
CACHE_BYTES = 256 * 1024 * 1024
CACHE_FILES = 1024
PAGE_SIZE = 48
MAX_DIAGNOSTICS = 2000
TABS = [
    ('appearance', '外观预设', True), ('hair_preset', '发型预设', True),
    ('clothing_preset', '服装预设', True), ('skin_preset', '皮肤预设', True),
    ('morph_preset', 'Morph 预设', True), ('pose', '姿势预设', True),
    ('general', '人物预设', True), ('animation', '动画预设', True),
    ('breast_physics', '胸部物理预设', True), ('glute_physics', '臀部物理预设', True),
    ('plugin_preset', '插件预设', True), ('other_preset', '其他预设', True),
    ('hair_item_preset', '单件发型预设', True), ('clothing_item_preset', '单件服装预设', True),
    ('hair', '头发', False), ('clothing', '服装', False),
    ('skin', '皮肤 / 纹理', False), ('morph', 'Morph', False), ('scene', '场景文件', False),
]
PRESETS = dict(appearance='appearance', hair='hair_preset', clothing='clothing_preset',
               skin='skin_preset', morphs='morph_preset', pose='pose', general='general',
               animationpresets='animation', breastphysics='breast_physics',
               glutephysics='glute_physics', plugins='plugin_preset')


def safe_member(name):
    name = name.replace('\\', '/')
    p = PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or ':' in name or '\x00' in name:
        raise ValueError('Unsafe archive member path')
    return str(p)


def classify(name):
    n = name.replace('\\', '/').lower()
    ext = PurePosixPath(n).suffix
    if ext == '.vap':
        prefix = 'custom/atom/person/'
        if n.startswith(prefix):
            return PRESETS.get(n[len(prefix):].split('/')[0], 'other_preset')
        if n.startswith('custom/clothing/'):
            return 'clothing_item_preset'
        if n.startswith('custom/hair/'):
            return 'hair_item_preset'
        if n.startswith('custom/pluginpresets/'):
            return 'plugin_preset'
        if n.startswith(('custom/', 'saves/')):
            return 'other_preset'
    if ext == '.vam' and n.startswith('custom/clothing/'):
        return 'clothing'
    if ext == '.vam' and n.startswith('custom/hair/'):
        return 'hair'
    if ext == '.vmi' and n.startswith('custom/atom/person/morphs/'):
        return 'morph'
    if ext in ('.jpg', '.jpeg', '.png', '.tga', '.dds') and n.startswith(
            ('custom/atom/person/textures/', 'custom/textures/')):
        return 'skin'
    if ext == '.json' and n.startswith('saves/person/'):
        category = n[len('saves/person/'):].split('/')[0]
        return PRESETS.get(category, 'general')
    if ext == '.json' and n.startswith('saves/scene/'):
        return 'scene'  # Do not imply that every scene contains a character.
    return None


def digest(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()


def atomic_json(path, value):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
    os.replace(tmp, path)


class Cancelled(Exception):
    pass


class Catalog:
    def __init__(self, data):
        self.data = Path(data).resolve()
        self.data.mkdir(parents=True, exist_ok=True)
        self.cache = self.data / 'Thumbnails'
        self.cache.mkdir(exist_ok=True)
        self.db = self.data / 'catalog.sqlite3'
        self.lock = threading.RLock()
        self.scan_lock = threading.Lock()
        self.cache_lock = threading.Lock()
        self.cancel = threading.Event()
        self.state = dict(running=False, phase='等待扫描', done=0, total=0, changed=0,
                          revision=0, last_scan=None, error='', cancelled=False)
        self.settings = dict(root='', auto=True, interval=60)
        settings_file = self.data / 'settings.json'
        if settings_file.exists():
            with contextlib.suppress(ValueError, OSError):
                self.settings.update(json.loads(settings_file.read_text('utf-8')))
        elif Path('F:/Virt A Mate').is_dir():
            self.settings['root'] = 'F:/Virt A Mate'
        with self.connect() as db:
            db.executescript('''
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);
                CREATE TABLE IF NOT EXISTS sources (
                    key TEXT PRIMARY KEY, signature TEXT, package TEXT, author TEXT, seen INTEGER);
                CREATE TABLE IF NOT EXISTS assets (
                    id TEXT PRIMARY KEY, source TEXT NOT NULL, path TEXT NOT NULL,
                    kind TEXT NOT NULL, name TEXT, author TEXT, package TEXT, tags TEXT,
                    folder TEXT, thumb TEXT, size INTEGER, created REAL, modified REAL,
                    stamp TEXT, diagnostic TEXT, search TEXT);
                CREATE INDEX IF NOT EXISTS asset_kind_name ON assets(kind, name COLLATE NOCASE, id);
                CREATE INDEX IF NOT EXISTS asset_kind_created ON assets(kind, created DESC, id);
                CREATE INDEX IF NOT EXISTS asset_source ON assets(source);
                CREATE INDEX IF NOT EXISTS asset_folder ON assets(kind, folder);
                CREATE TABLE IF NOT EXISTS diagnostics (
                    id INTEGER PRIMARY KEY, source TEXT, path TEXT, message TEXT, time REAL);
            ''')
            version = db.execute("SELECT value FROM meta WHERE key='schema'").fetchone()
            if version and int(version[0]) != SCHEMA:
                raise RuntimeError('Unsupported index schema; back up and remove catalog.sqlite3')
            db.execute("INSERT OR REPLACE INTO meta VALUES('schema',?)", (str(SCHEMA),))
        self.trim_cache()

    def connect(self):
        db = sqlite3.connect(self.db, timeout=15)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA cache_size=-4096')
        db.execute('PRAGMA busy_timeout=15000')
        return ClosingConnection(db)

    def update_state(self, **kwargs):
        with self.lock:
            self.state.update(kwargs)

    def status(self):
        with self.lock:
            result = dict(self.state, settings=dict(self.settings))
        with self.connect() as db:
            result['counts'] = dict(db.execute('SELECT kind,COUNT(*) FROM assets GROUP BY kind'))
            result['diagnostics'] = db.execute('SELECT COUNT(*) FROM diagnostics').fetchone()[0]
            result['sources'] = db.execute('SELECT COUNT(*) FROM sources').fetchone()[0]
        result['limits'] = dict(page=PAGE_SIZE, thumbnail_requests=2, scan_workers=1,
                                http_workers=4, disk_cache_mb=256, disk_cache_files=CACHE_FILES,
                                metadata_kb=256, detail_mb=2, image_mb=8)
        result['index_path'] = str(self.db)
        result['tabs'] = TABS
        return result

    def configure(self, root, auto=True):
        if self.state['running']:
            raise ValueError('请先取消扫描，等待结束后再更改目录。')
        p = Path(root).expanduser().resolve()
        if not p.is_dir() or not any((p / n).is_dir() for n in ('Custom', 'AddonPackages', 'Saves')):
            raise ValueError('请选择含有 Custom、AddonPackages 或 Saves 的 VaM 根目录。')
        if self.data == p or self.data.is_relative_to(p):
            raise ValueError('索引目录不能位于 VaM 来源目录内。')
        with self.lock:
            self.settings.update(root=str(p), auto=bool(auto))
            atomic_json(self.data / 'settings.json', self.settings)
        return self.start_scan()

    def check_cancel(self):
        if self.cancel.is_set():
            raise Cancelled()

    def diagnostic(self, db, source, path, message):
        db.execute('INSERT INTO diagnostics(source,path,message,time) VALUES(?,?,?,?)',
                   (source, path, str(message)[:2000], time.time()))
        db.execute('DELETE FROM diagnostics WHERE id <= (SELECT MAX(id)-? FROM diagnostics)', (MAX_DIAGNOSTICS,))

    def start_scan(self, force=False):
        if not self.settings['root']:
            raise ValueError('请先设置 VaM 目录。')
        if not self.scan_lock.acquire(blocking=False):
            return dict(started=False)
        self.cancel.clear()
        self.update_state(running=True, phase='枚举目录', done=0, total=0, changed=0,
                          error='', cancelled=False)
        threading.Thread(target=self.scan, args=(force,), daemon=True, name='VaM-Index').start()
        return dict(started=True)

    def walk(self, base):
        if not base.exists():
            return
        def fail(error):
            # Abort deletion reconciliation on an incomplete filesystem inventory.
            raise error
        for folder, dirs, files in os.walk(base, followlinks=False, onerror=fail):
            self.check_cancel()
            dirs[:] = [d for d in dirs if not (Path(folder) / d).is_symlink()
                       and not (hasattr(os.path, 'isjunction') and os.path.isjunction(Path(folder) / d))]
            yield Path(folder), files

    def inventory(self, root):
        sources = []
        for folder, files in self.walk(root / 'AddonPackages'):
            for name in files:
                self.check_cancel()
                if name.lower().endswith('.var'):
                    p = folder / name
                    if p.is_symlink() or not p.resolve().is_relative_to(root):
                        continue
                    st = p.stat()
                    sources.append((p.relative_to(root).as_posix(), f'{st.st_size}:{st.st_mtime_ns}', None))
        for top in ('Custom', 'Saves'):
            for folder, files in self.walk(root / top):
                # Directory-local grouping bounds memory, and catches thumbnail changes too.
                paths = []
                signatures = []
                for name in sorted(files):
                    self.check_cancel()
                    p = folder / name
                    rel = p.relative_to(root).as_posix()
                    if classify(rel) or p.suffix.lower() in ('.jpg', '.jpeg', '.png'):
                        if p.is_symlink() or not p.resolve().is_relative_to(root):
                            continue
                        st = p.stat()
                        paths.append((rel, st))
                        signatures.append(f'{name}:{st.st_size}:{st.st_mtime_ns}')
                if any(classify(n) for n, _ in paths):
                    sources.append(('loose:' + folder.relative_to(root).as_posix(),
                                    digest('\n'.join(signatures)), paths))
        return sources

    def scan(self, force):
        started = time.monotonic()
        try:
            root = Path(self.settings['root']).resolve()
            if not root.is_dir():
                raise ValueError('来源目录不可访问；保留现有索引。')
            # Validate the full inventory before deleting anything from the last scan.
            sources = self.inventory(root)
            self.check_cancel()
            generation = time.time_ns()
            self.update_state(total=len(sources), phase='更新索引')
            with self.connect() as db:
                old_root = db.execute("SELECT value FROM meta WHERE key='root'").fetchone()
                if not old_root or old_root[0] != str(root):
                    db.execute('DELETE FROM assets')
                    db.execute('DELETE FROM sources')
                    db.execute('DELETE FROM diagnostics')
                    db.execute("INSERT OR REPLACE INTO meta VALUES('root',?)", (str(root),))
                    db.commit()
                    force = True
                classifier = db.execute("SELECT value FROM meta WHERE key='classifier'").fetchone()
                if not classifier or classifier[0] != CLASSIFIER_VERSION:
                    force = True
                old = {r['key']: r['signature'] for r in db.execute('SELECT key,signature FROM sources')}
                changed = 0
                for i, (key, signature, paths) in enumerate(sources):
                    self.check_cancel()
                    self.update_state(done=i, phase=key)
                    if not force and old.get(key) == signature:
                        db.execute('UPDATE sources SET seen=? WHERE key=?', (generation, key))
                        continue
                    # Source-level atomic replacement; cancellation rolls back current source.
                    try:
                        db.commit()
                        db.execute('BEGIN')
                        db.execute('DELETE FROM assets WHERE source=?', (key,))
                        db.execute('DELETE FROM diagnostics WHERE source=?', (key,))
                        if paths is None:
                            package, author = self.index_zip(db, root, key, signature)
                        else:
                            package, author = '', ''
                            names = {n.lower(): n for n, _ in paths}
                            for n, st in paths:
                                self.check_cancel()
                                if classify(n):
                                    self.add_asset(db, root, key, n, signature, names, st.st_size,
                                                   st.st_ctime, st.st_mtime, '', '',
                                                   lambda path, limit: self.read_loose(root, path, limit))
                        db.execute('INSERT OR REPLACE INTO sources VALUES(?,?,?,?,?)',
                                   (key, signature, package, author, generation))
                        self.check_cancel()
                        db.commit()
                    except Cancelled:
                        db.rollback()
                        raise
                    except Exception as error:
                        db.rollback()
                        # Keep last known entries on corruption, with a prominent source diagnostic.
                        db.execute('DELETE FROM diagnostics WHERE source=?', (key,))
                        self.diagnostic(db, key, key, error)
                        db.execute('UPDATE assets SET diagnostic=? WHERE source=?',
                                   ('来源损坏，显示上次索引：' + str(error)[:1500], key))
                        db.execute('INSERT OR REPLACE INTO sources VALUES(?,?,?,?,?)',
                                   (key, ('retry:' if isinstance(error, OSError) else '') + signature,
                                    '', '', generation))
                        db.commit()
                    changed += 1
                    self.update_state(changed=changed)
                self.check_cancel()
                removed = db.execute('SELECT COUNT(*) FROM sources WHERE seen<>?', (generation,)).fetchone()[0]
                db.execute('DELETE FROM assets WHERE source IN (SELECT key FROM sources WHERE seen<>?)', (generation,))
                db.execute('DELETE FROM diagnostics WHERE source IN (SELECT key FROM sources WHERE seen<>?)', (generation,))
                db.execute('DELETE FROM sources WHERE seen<>?', (generation,))
                db.execute("INSERT OR REPLACE INTO meta VALUES('classifier',?)", (CLASSIFIER_VERSION,))
                db.commit()
            self.update_state(done=len(sources), phase=f'扫描完成 · {time.monotonic()-started:.1f} 秒',
                              changed=changed + removed, last_scan=time.time(),
                              revision=self.state['revision'] + (1 if changed or removed else 0))
        except Cancelled:
            self.update_state(phase='已取消 · 保留已完成来源，未执行删除清理', cancelled=True,
                              revision=self.state['revision'] + 1)
        except Exception as error:
            logging.exception('Scan failed')
            self.update_state(phase='扫描失败 · 保留已有索引', error=str(error))
        finally:
            self.update_state(running=False)
            self.scan_lock.release()

    def open_zip(self, path):
        # Bound central-directory allocation before ZipFile builds its entry list.
        with open(path, 'rb') as f:
            end = zipfile._EndRecData(f)
        if not end or end[zipfile._ECD_SIZE] > 64 * 1024 * 1024 or end[zipfile._ECD_ENTRIES_TOTAL] > 200000:
            raise ValueError('Invalid ZIP or central directory exceeds 64 MiB / 200,000 entries')
        return zipfile.ZipFile(path, 'r')

    def index_zip(self, db, root, key, signature):
        package = Path(key).stem
        author = package.split('.')[0]
        with self.open_zip(root / key) as z:
            names = {}
            for info in z.infolist():
                self.check_cancel()
                if info.is_dir():
                    continue
                try:
                    n = safe_member(info.filename)
                except ValueError as error:
                    self.diagnostic(db, key, info.filename, error)
                    continue
                if n.lower() in names:
                    self.diagnostic(db, key, n, 'Duplicate archive path (case insensitive); first entry retained')
                    continue
                names[n.lower()] = info.filename
            meta = {}
            if 'meta.json' in names:
                try:
                    meta = self.read_json(self.read_zip(z, names['meta.json'], META_LIMIT))
                    author = str(meta.get('creatorName') or author)
                except Exception as error:
                    self.diagnostic(db, key, 'meta.json', error)
            for normalized, original in names.items():
                self.check_cancel()
                n = safe_member(original)
                if not classify(n):
                    continue
                info = z.getinfo(original)
                try:
                    created = dt.datetime(*info.date_time).timestamp()
                except (ValueError, OSError):
                    created = 0
                self.add_asset(db, root, key, n, signature, names, info.file_size,
                               created, created, package, author,
                               lambda path, limit: self.read_zip(z, names.get(path.lower(), path), limit),
                               meta.get('tags', ''))
        return package, author

    @staticmethod
    def read_json(data):
        obj = json.loads(data.decode('utf-8-sig'))
        if not isinstance(obj, dict):
            raise ValueError('Metadata is not a JSON object')
        return obj

    @staticmethod
    def read_zip(z, name, limit):
        info = z.getinfo(name)
        if info.file_size > limit:
            raise ValueError(f'File exceeds read limit ({limit // 1024} KiB)')
        with z.open(info) as stream:
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ValueError('Decompressed data exceeds read limit')
        return data

    @staticmethod
    def read_loose(root, name, limit):
        p = (root / safe_member(name)).resolve()
        if not p.is_relative_to(root):
            raise ValueError('Path escapes source root')
        if p.stat().st_size > limit:
            raise ValueError(f'File exceeds read limit ({limit // 1024} KiB)')
        with p.open('rb') as f:
            data = f.read(limit + 1)
        if len(data) > limit:
            raise ValueError('File grew beyond read limit')
        return data

    def add_asset(self, db, root, source, path, signature, names, size, created, modified,
                  package, author, reader, package_tags=''):
        kind = classify(path)
        stem = str(PurePosixPath(path).with_suffix(''))
        name = PurePosixPath(path).stem.removeprefix('Preset_')
        tags = package_tags
        diagnostic = ''
        if PurePosixPath(path).suffix.lower() in ('.vam', '.vmi'):
            try:
                metadata = self.read_json(reader(path, META_LIMIT))
                name = str(metadata.get('displayName') or name)
                author = str(metadata.get('creatorName') or author)
                tags = metadata.get('tags') or metadata.get('region') or metadata.get('group') or tags
            except Exception as error:
                diagnostic = str(error)
                self.diagnostic(db, source, path, error)
        if not author and kind in ('clothing', 'hair', 'morph'):
            parts = PurePosixPath(path).parts
            author = parts[5] if kind == 'morph' and len(parts) > 6 else (parts[3] if len(parts) > 4 else '')
        if isinstance(tags, (dict, list)):
            tags = json.dumps(tags, ensure_ascii=False)
        tags = str(tags or '')[:4096]
        thumb = ''
        for ext in ('.jpg', '.jpeg', '.png'):
            if (stem + ext).lower() in names:
                thumb = safe_member(names[(stem + ext).lower()])
                break
        if kind == 'skin' and PurePosixPath(path).suffix.lower() in ('.jpg', '.jpeg', '.png'):
            thumb = path
        folder = ('用户保存 / ' if not package else package + ' / ') + str(PurePosixPath(path).parent)
        identity = digest(str(root).casefold() + '\0' + source.casefold() + '\0' + path.casefold())
        search = ' '.join((name, author, package, tags, path)).casefold()
        db.execute('INSERT OR REPLACE INTO assets VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
                   (identity, source, path, kind, name, author, package, tags, folder, thumb,
                    size, created, modified, signature, diagnostic, search))

    def query(self, args):
        kind = str(args.get('kind', 'appearance'))
        page = max(0, min(int(args.get('page', 0)), 1000000))
        where, params = ['kind=?'], [kind]
        for term in str(args.get('search', ''))[:300].casefold().split()[:12]:
            where.append('instr(search,?)>0')
            params.append(term)
        source = args.get('source', '')
        if source == 'user':
            where.append("package=''")
        elif source == 'package':
            where.append("package<>''")
        if args.get('folder'):
            where.append('folder=?')
            params.append(str(args['folder']))
        if args.get('author'):
            where.append('instr(lower(author),?)>0')
            params.append(str(args['author']).lower()[:200])
        if args.get('tag'):
            where.append('instr(lower(tags),?)>0')
            params.append(str(args['tag']).lower()[:200])
        order = {'name': 'name COLLATE NOCASE ASC,id', 'newest': 'created DESC,id',
                 'oldest': 'created ASC,id'}.get(args.get('sort'), 'name COLLATE NOCASE ASC,id')
        sql = ' FROM assets WHERE ' + ' AND '.join(where)
        with self.connect() as db:
            total = db.execute('SELECT COUNT(*)' + sql, params).fetchone()[0]
            page = min(page, max(0, (total - 1) // PAGE_SIZE))
            rows = [dict(r) for r in db.execute('SELECT *' + sql + ' ORDER BY ' + order + ' LIMIT ? OFFSET ?',
                                               params + [PAGE_SIZE, page * PAGE_SIZE])]
        for row in rows:
            row.pop('search', None)
            row['thumb'] = bool(row['thumb'])
        return dict(items=rows, total=total, page=page, page_size=PAGE_SIZE)

    def folders(self, args):
        # Sidebar itself is paged, so a huge package library cannot create a huge DOM.
        term = str(args.get('search', ''))[:200].casefold()
        offset = max(0, int(args.get('offset', 0)))
        with self.connect() as db:
            rows = [dict(r) for r in db.execute('''SELECT folder,COUNT(*) AS count FROM assets
                WHERE kind=? AND instr(lower(folder),?)>0 GROUP BY folder ORDER BY folder LIMIT 101 OFFSET ?''',
                (args.get('kind', 'appearance'), term, offset))]
        return dict(items=rows[:100], more=len(rows) > 100)

    def asset(self, identity):
        with self.connect() as db:
            row = db.execute('SELECT * FROM assets WHERE id=?', (identity,)).fetchone()
            root = db.execute("SELECT value FROM meta WHERE key='root'").fetchone()
        if not row or not root:
            raise ValueError('资源已移除，请刷新列表。')
        return dict(row), Path(root[0])

    def read_asset(self, row, root, path, limit):
        if row['source'].startswith('loose:'):
            return self.read_loose(root, path, limit)
        source = (root / safe_member(row['source'])).resolve()
        if not source.is_relative_to(root):
            raise ValueError('Archive escapes source root')
        with self.open_zip(source) as z:
            try:
                return self.read_zip(z, path, limit)
            except KeyError:
                for info in z.infolist():
                    if info.filename.replace('\\', '/').casefold() == path.casefold():
                        return self.read_zip(z, info.filename, limit)
                raise

    def detail(self, identity):
        row, root = self.asset(identity)
        row.pop('search', None)
        row['location'] = str(root / row['source']) + ' :: ' + row['path'] if row['package'] else str(root / row['path'])
        row['time_note'] = 'VAR 保存的是 ZIP 条目时间，并非可靠的原始创建时间。' if row['package'] else '松散资源使用文件系统创建时间。'
        row['json'] = ''
        if row['kind'] != 'skin':
            try:
                obj = self.read_json(self.read_asset(row, root, row['path'], DETAIL_LIMIT))
                pretty = json.dumps(obj, ensure_ascii=False, indent=2)
                encoded = pretty.encode('utf-8')
                row['json'] = encoded[:65536].decode('utf-8', errors='ignore')
                row['truncated'] = len(encoded) > 65536
            except Exception as error:
                row['diagnostic'] = str(error)
                with self.connect() as db:
                    db.execute('DELETE FROM diagnostics WHERE source=? AND path=?', (row['source'], row['path']))
                    self.diagnostic(db, row['source'], row['path'], error)
        return row

    def trim_cache(self):
        files = []
        for p in self.cache.iterdir():
            if p.is_file():
                st = p.stat()
                files.append((st.st_mtime, st.st_size, p))
        total = sum(f[1] for f in files)
        count = len(files)
        for _, size, path in sorted(files):
            if total <= CACHE_BYTES and count <= CACHE_FILES:
                break
            path.unlink(missing_ok=True)
            total -= size
            count -= 1

    def thumbnail(self, identity):
        row, root = self.asset(identity)
        if not row['thumb']:
            raise ValueError('没有缩略图')
        ext = PurePosixPath(row['thumb']).suffix.lower()
        cache_key = digest(identity + row['stamp'] + row['thumb']) + ext
        path = self.cache / cache_key
        with self.cache_lock:
            if path.exists():
                data = path.read_bytes()
                os.utime(path, None)
                return data, ext
        try:
            data = self.read_asset(row, root, row['thumb'], IMAGE_LIMIT)
            validate_image(data)
        except Exception as error:
            with self.connect() as db:
                db.execute('DELETE FROM diagnostics WHERE source=? AND path=?', (row['source'], row['thumb']))
                self.diagnostic(db, row['source'], row['thumb'], error)
            raise
        with self.cache_lock:
            path.write_bytes(data)
            self.trim_cache()
        return data, ext

    def diagnostics(self, args):
        with self.connect() as db:
            rows = [dict(r) for r in db.execute('SELECT * FROM diagnostics ORDER BY id DESC LIMIT 100 OFFSET ?',
                                               (max(0, int(args.get('offset', 0))),))]
        return dict(items=rows)


class ClosingConnection:
    """sqlite's own context manager commits but does not close connections."""
    def __init__(self, db): self.db = db
    def __enter__(self): return self.db
    def __exit__(self, typ, val, tb):
        try:
            self.db.rollback() if typ else self.db.commit()
        finally:
            self.db.close()


def validate_image(data):
    """Check dimensions before handing untrusted image data to the browser decoder."""
    width = height = 0
    if data.startswith(b'\x89PNG\r\n\x1a\n') and len(data) >= 24:
        width, height = struct.unpack('>II', data[16:24])
    elif data.startswith(b'\xff\xd8'):
        i = 2
        while i + 4 <= len(data):
            if data[i] != 255:
                break
            while i < len(data) and data[i] == 255: i += 1
            if i >= len(data): break
            marker = data[i]
            i += 1
            if marker in (0xD8, 0xD9, 0x01) or 0xD0 <= marker <= 0xD7: continue
            length = int.from_bytes(data[i:i+2], 'big')
            if length < 2 or i + length > len(data): break
            if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                height, width = struct.unpack('>HH', data[i+3:i+7])
                break
            i += length
    if not width or not height or width * height > 16 * 1024 * 1024 or max(width, height) > 8192:
        raise ValueError('Unsupported/corrupt image or image exceeds 16 MP / 8192 pixels')


class BoundedServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, handler):
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(address, handler)
    def process_request(self, request, address):
        self.slots.acquire()
        try:
            super().process_request(request, address)
        except Exception:
            self.slots.release()
            raise
    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


def serve(data, parent=0):
    catalog = Catalog(data)
    token = secrets.token_urlsafe(32)
    ui = Path(__file__).resolve().parent.parent / 'Web'

    class Handler(http.server.BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(15)
        def log_message(self, *_): pass
        def respond(self, status, data, mime='application/json; charset=utf-8'):
            if not isinstance(data, bytes):
                data = json.dumps(data, ensure_ascii=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob: data:; connect-src 'self'; frame-ancestors 'none'")
            self.end_headers()
            with contextlib.suppress(BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                self.wfile.write(data)
        def route(self):
            parsed = urllib.parse.urlsplit(self.path)
            prefix = '/' + token + '/'
            if not parsed.path.startswith(prefix):
                raise PermissionError('Invalid session')
            host = self.headers.get('Host', '')
            if host != f'127.0.0.1:{self.server.server_port}':
                raise PermissionError('Invalid host')
            return parsed.path[len(prefix):], dict(urllib.parse.parse_qsl(parsed.query))
        def do_GET(self):
            try:
                route, args = self.route()
                if route in ('', 'index.html', 'app.js', 'style.css'):
                    file = route or 'index.html'
                    mime = {'index.html': 'text/html; charset=utf-8', 'app.js': 'application/javascript; charset=utf-8', 'style.css': 'text/css; charset=utf-8'}[file]
                    self.respond(200, (ui / file).read_bytes(), mime)
                elif route == 'api/state': self.respond(200, catalog.status())
                elif route == 'api/query': self.respond(200, catalog.query(args))
                elif route == 'api/folders': self.respond(200, catalog.folders(args))
                elif route == 'api/detail': self.respond(200, catalog.detail(args.get('id', '')))
                elif route == 'api/diagnostics': self.respond(200, catalog.diagnostics(args))
                elif route == 'api/thumb':
                    image, ext = catalog.thumbnail(args.get('id', ''))
                    self.respond(200, image, 'image/png' if ext == '.png' else 'image/jpeg')
                else: self.respond(404, dict(error='Not found'))
            except PermissionError as error: self.respond(403, dict(error=str(error)))
            except Exception as error: self.respond(400, dict(error=str(error)))
        def do_POST(self):
            try:
                route, _ = self.route()
                expected = f'http://127.0.0.1:{self.server.server_port}'
                if self.headers.get('Origin') != expected:
                    raise PermissionError('Invalid request origin')
                length = int(self.headers.get('Content-Length', '0'))
                if length < 0 or length > 8192: raise ValueError('Request too large')
                args = json.loads(self.rfile.read(length) or b'{}')
                if route == 'api/config': result = catalog.configure(str(args.get('root', '')), args.get('auto', True))
                elif route == 'api/scan': result = catalog.start_scan(bool(args.get('force', False)))
                elif route == 'api/cancel':
                    catalog.cancel.set()
                    result = dict(cancelled=True)
                else: raise ValueError('Unknown operation')
                self.respond(200, result)
            except PermissionError as error: self.respond(403, dict(error=str(error)))
            except Exception as error: self.respond(400, dict(error=str(error)))

    server = BoundedServer(('127.0.0.1', 0), Handler)
    url = f'http://127.0.0.1:{server.server_port}/{token}/'
    atomic_json(catalog.data / 'ready.json', dict(url=url, pid=os.getpid()))

    def monitor():
        last = 0
        while True:
            time.sleep(2)
            if parent and os.name == 'nt':
                kernel = ctypes.windll.kernel32
                kernel.OpenProcess.restype = ctypes.c_void_p
                handle = kernel.OpenProcess(0x1000, False, parent)
                if not handle: os._exit(0)
                code = ctypes.c_ulong()
                kernel.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code))
                kernel.CloseHandle(ctypes.c_void_p(handle))
                if code.value != 259: os._exit(0)
            now = time.monotonic()
            if catalog.settings['auto'] and catalog.settings['root'] and not catalog.state['running'] and now - last > catalog.settings['interval']:
                last = now
                with contextlib.suppress(Exception): catalog.start_scan()
    threading.Thread(target=monitor, daemon=True, name='VaM-Watch').start()
    server.serve_forever(poll_interval=0.5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', required=True)
    parser.add_argument('--serve', action='store_true')
    parser.add_argument('--parent', type=int, default=0)
    parser.add_argument('--scan')
    args = parser.parse_args()
    Path(args.data).mkdir(parents=True, exist_ok=True)
    from logging.handlers import RotatingFileHandler
    logging.basicConfig(handlers=[RotatingFileHandler(Path(args.data) / 'service.log', maxBytes=1024*1024, backupCount=2, encoding='utf-8')], level=logging.INFO)
    try:
        if args.serve:
            # A single index writer/service per plugin data directory, including across UE instances.
            lock_file = open(Path(args.data) / 'service.lock', 'a+b')
            if os.name == 'nt':
                import msvcrt
                lock_file.seek(0)
                if not lock_file.read(1):
                    lock_file.write(b'0')
                    lock_file.flush()
                lock_file.seek(0)
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
            serve(args.data, args.parent)
        elif args.scan:
            cat = Catalog(args.data)
            cat.configure(args.scan, auto=False)
            while cat.state['running']: time.sleep(0.1)
            print(json.dumps(cat.status(), ensure_ascii=False))
    except Exception:
        logging.exception('VaM index service failed')
        raise


if __name__ == '__main__':
    main()
