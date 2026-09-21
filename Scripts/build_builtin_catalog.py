"""Offline metadata catalog builder. Requires UnityPy==1.25.3, never runs VaM.

The browser itself needs only the generated JSON and Python's standard library.
Mesh/texture objects are never read; morph delta arrays are skipped by byte size.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path


def build(root, output):
    import UnityPy
    from UnityPy.helpers.TypeTreeHelper import FUNCTION_READ_MAP, metaflag_is_aligned
    from UnityPy.streams import EndianBinaryReader

    base = root / 'VaM_Data/StreamingAssets'
    files, entries = {}, []

    def fingerprint(name):
        path = base / name
        if name not in files:
            before = path.stat()
            with path.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            after = path.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError('Source changed: ' + name)
            files[name] = {'path': path.relative_to(root).as_posix(), 'sha256': digest, 'size': after.st_size}
        return name

    def metadata(node, reader):
        aligned = metaflag_is_aligned(node.m_MetaFlag)
        if node.m_Type in FUNCTION_READ_MAP:
            result = FUNCTION_READ_MAP[node.m_Type](reader)
        elif node.m_Children and node.m_Children[0].m_Type == 'Array':
            array = node.m_Children[0]
            aligned |= metaflag_is_aligned(array.m_MetaFlag)
            count = reader.read_int()
            if count < 0 or count > 10_000_000:
                raise ValueError('Invalid array length')
            subtype = array.m_Children[1]
            if node.m_Name == 'deltas':
                if subtype.m_Type != 'DAZMorphVertex' or subtype.m_ByteSize != 16:
                    raise ValueError('Unsupported morph delta layout')
                reader.Position += count * 16
                result = {'skipped_count': count}
            else:
                result = [metadata(subtype, reader) for _ in range(count)]
        else:
            result = {child.m_Name: metadata(child, reader) for child in node.m_Children}
        if aligned:
            reader.align_stream()
        return result

    env = UnityPy.load(str(base / 'StandaloneWindows64'))
    manifest = next(o for o in env.objects if o.type.name == 'AssetBundleManifest').read_typetree()
    names = dict(manifest['AssetBundleNames'])
    deps = {names[key]: [names[x] for x in info['AssetBundleDependencies']]
            for key, info in manifest['AssetBundleInfos']}
    fingerprint('StandaloneWindows64')

    def closure(bundle):
        found, pending = set(), [bundle]
        while pending:
            name = pending.pop()
            if name in found:
                continue
            found.add(name)
            fingerprint(name)
            pending.extend(deps[name])
        return sorted(found)

    # The Person prefab is the actual selectable registry, not benchmark copies.
    env = UnityPy.load(str(base / 'a_per'))
    fingerprint('a_per')
    containers = {}
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        fields = {n.m_Name for n in obj.serialized_type.node.m_Children}
        if 'assetBundleName' not in fields:
            continue
        data = obj.read_typetree()
        script = obj.assets_file.objects[data['m_Script']['m_PathID']].read().m_ClassName
        role = {'DAZCharacter': 'character', 'DAZHairGroup': 'hair', 'DAZClothingItem': 'clothing'}.get(script)
        if not role or not data.get('displayName'):
            continue
        name = data['displayName']
        bundle, asset = data['assetBundleName'], data['assetName']
        gender = ('male' if data['isMale'] else 'female') if role == 'character' else {0: 'male', 1: 'female', 2: 'any'}.get(data['gender'], 'unknown')
        evidence = {'file': 'a_per', 'object': str(obj.path_id), 'class': script, 'field': 'displayName'}
        if not bundle:
            # Only the evidenced empty hair selector has defined no-resource semantics.
            if role == 'hair' and name == 'No Hair' and not data['prefab']['m_PathID']:
                entries.append({'role': role, 'names': [name], 'gender': gender, 'operation': 'clear_hair',
                                'evidence': evidence, 'files': ['a_per'], 'locator': {'operation': 'clear_hair'}})
            continue
        required = closure(bundle)
        if bundle not in containers:
            target_env = UnityPy.load(str(base / bundle))
            containers[bundle] = {path: str(ptr.path_id) for path, ptr in target_env.container.items()}
        matches = [(p, oid) for p, oid in containers[bundle].items() if Path(p).stem.casefold() == asset.casefold()]
        if len(matches) != 1:
            raise ValueError(f'Ambiguous container: {name} {bundle} {asset} {matches}')
        path, oid = matches[0]
        entries.append({'role': role, 'names': [name], 'gender': gender, 'operation': 'convert_builtin',
                        'evidence': evidence, 'files': sorted(set(required + ['a_per', 'StandaloneWindows64'])),
                        'locator': {'file': bundle, 'asset': path, 'object': oid}})

    for bundle, gender in [('f_mb', 'female'), ('m_mb', 'male')]:
        env = UnityPy.load(str(base / bundle))
        fingerprint(bundle)
        for obj in env.objects:
            if obj.type.name != 'MonoBehaviour':
                continue
            if '_morphs' not in {n.m_Name for n in obj.serialized_type.node.m_Children}:
                continue
            raw = obj.get_raw_data()
            reader = EndianBinaryReader(raw, endian=obj.reader.endian)
            data = metadata(obj.serialized_type.node, reader)
            if reader.Position != len(raw):
                raise ValueError('Morph metadata layout mismatch')
            for index, morph in enumerate(data['_morphs']):
                if morph['disable']:
                    continue
                aliases = sorted({morph[k] for k in ('morphName', 'displayName', 'overrideName') if morph.get(k)})
                entries.append({'role': 'morph', 'names': aliases, 'gender': gender, 'operation': 'convert_builtin',
                    'files': [bundle], 'evidence': {'file': bundle, 'object': str(obj.path_id), 'field': f'_morphs/{index}'},
                    'locator': {'file': bundle, 'object': str(obj.path_id), 'morph_index': index, 'morph_name': morph['morphName']},
                    'metadata': {k: morph[k] for k in ('displayName', 'overrideName', 'region', 'group', 'min', 'max', 'numDeltas', 'isPoseControl')}})
    result = {'schema': 1, 'generator': 'UnityPy-1.25.3/metadata-only-v1', 'files': files,
              'entries': sorted(entries, key=lambda x: json.dumps(x, sort_keys=True))}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2), encoding='utf-8')
    print(dict(collections.Counter(e['role'] for e in entries)), 'files', len(files))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build(args.root.resolve(), args.output.resolve())
