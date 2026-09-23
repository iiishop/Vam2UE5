"""Strict, standard-library source decoders. No VaM code is executed.

All original indices/parameters survive in source records. Preview coordinates
are UE centimetres (X,Y,Z) = 100 * VaM (z,x,y); UV = (u,1-v).
"""
import hashlib
import json
import math
import struct
from functools import lru_cache

MAX_COUNT = 2_000_000
MAX_BYTES = 256 * 1024 * 1024


@lru_cache(maxsize=32)
def _record_struct(fmt):
    return struct.Struct('<' + fmt)


class DecodeError(ValueError):
    def __init__(self, code, message, offset=None):
        self.code, self.offset = code, offset
        super().__init__(f'{code}: {message}' + (f' (byte {offset})' if offset is not None else ''))


def require(test, code, message):
    if not test: raise DecodeError(code, message)


def finite(value, path='value'):
    if isinstance(value, float):
        require(math.isfinite(value), 'non_finite', path)
    elif isinstance(value, dict):
        for k, v in value.items():
            if isinstance(v, float):
                if not math.isfinite(v): raise DecodeError('non_finite', path + '/' + str(k))
            elif isinstance(v, (dict, list, tuple)):
                finite(v, path + '/' + str(k))
    elif isinstance(value, (list, tuple)):
        for i, v in enumerate(value):
            if isinstance(v, float):
                if not math.isfinite(v): raise DecodeError('non_finite', path + '/' + str(i))
            elif isinstance(v, (dict, list, tuple)):
                finite(v, path + '/' + str(i))


def to_ue(v): return [v[2]*100, v[0]*100, v[1]*100]
def from_ue(v): return [v[1]/100, v[2]/100, v[0]/100]
def uv_to_ue(v): return [v[0], 1-v[1]]
def vec(v, n=3): return [v[k] for k in 'xyzw'[:n]]


class Reader:
    def __init__(self, data):
        require(len(data) <= MAX_BYTES, 'read_limit', 'Binary exceeds 256 MiB')
        self.data, self.pos = data, 0

    def take(self, n):
        if n < 0 or self.pos+n > len(self.data):
            raise DecodeError('truncated', f'Need {n} bytes; {len(self.data)-self.pos} remain', self.pos)
        b = self.data[self.pos:self.pos+n]; self.pos += n
        return b

    def record(self, fmt):
        layout = _record_struct(fmt)
        end = self.pos + layout.size
        if end > len(self.data):
            raise DecodeError('truncated', f'Need {layout.size} bytes; {len(self.data)-self.pos} remain', self.pos)
        result = layout.unpack_from(self.data, self.pos)
        self.pos = end
        for index, value in enumerate(result):
            if isinstance(value, float) and not math.isfinite(value):
                raise DecodeError('non_finite', 'byte '+str(end)+'/'+str(index))
        return list(result)

    def integer(self): return self.record('i')[0]
    def number(self): return self.record('f')[0]
    def count(self, stride=1):
        n = self.integer()
        require(0 <= n <= MAX_COUNT, 'invalid_count', f'{n} at byte {self.pos-4}')
        require(n*stride <= len(self.data)-self.pos, 'truncated', f'Array {n} x {stride} at byte {self.pos}')
        return n

    def boolean(self):
        value = self.take(1)[0]
        require(value in (0, 1), 'invalid_boolean', str(self.pos-1))
        return bool(value)

    def string(self):
        n = 0
        for shift in range(0, 35, 7):
            b = self.take(1)[0]; n |= (b & 127) << shift
            if not b & 128: break
        else: raise DecodeError('invalid_string', 'Invalid 7-bit length', self.pos)
        require(n <= 1024*1024, 'invalid_string', 'String too large')
        try: return self.take(n).decode('utf-8', errors='strict')
        except UnicodeError as e: raise DecodeError('invalid_string', str(e), self.pos) from e

    def header(self, name, versions=('1.0',)):
        actual, version = self.string(), self.string()
        require(actual == name, 'unknown_section', f'Expected {name}, got {actual}')
        require(version in versions, 'unsupported_version', f'{name} {version}')
        return version

    def array(self, fmt):
        stride = struct.calcsize('<'+fmt)
        return [self.record(fmt) for _ in range(self.count(stride))]

    def end(self): require(self.pos == len(self.data), 'trailing_data', f'{len(self.data)-self.pos} unexpected bytes at {self.pos}')


def decode_vmb(data, meta, vertex_count=None, allow_repeated=False):
    r = Reader(data); n = r.count(16)
    require(str(meta.get('numDeltas')) == str(n), 'delta_count_mismatch', 'VMI numDeltas differs from VMB')
    deltas = [r.record('ifff') for _ in range(n)]; r.end()
    seen = set()
    for i, *delta in deltas:
        require(i >= 0 and (vertex_count is None or i < vertex_count), 'vertex_index', str(i))
        require(allow_repeated or i not in seen, 'duplicate_delta', str(i)); seen.add(i)
    finite(meta)
    return {'layout': 'VMB/count-i32+index-i32-xyz-f32', 'parameters': meta, 'deltas': deltas,
            'sha256': hashlib.sha256(data).hexdigest()}


def mesh_record(r):
    r.header('DAZMesh')
    names = {k: r.string() for k in ('nodeId', 'sceneNodeId', 'geometryId', 'sceneGeometryId')}
    vertices = r.array('fff')
    materials = [r.string() for _ in range(r.count())]
    n = r.count(8)
    def polygons():
        result = []
        for _ in range(n):
            material = r.integer(); count = r.count(4)
            require(count in (3, 4), 'polygon_layout', f'Only evidenced triangles/quads supported: {count}')
            result.append({'materialNum': material, 'vertices': [r.integer() for _ in range(count)]})
        return result
    polys, uvpolys = polygons(), polygons()
    uv = r.array('ff'); maps = r.array('iii')
    return {'names': names, 'vertices': vertices, 'materials': materials, 'polygons': polys,
            'uv_polygons': uvpolys, 'uv': uv,
            'uv_map': [dict(zip(('fromvert','tovert','polyindex'), v)) for v in maps]}


def cloth_record(r):
    r.header('ClothGeometryData')
    data = {'triangles': [x[0] for x in r.array('i')], 'particles': r.array('fff'),
            'mesh_to_physics': [x[0] for x in r.array('i')], 'physics_to_mesh': [x[0] for x in r.array('i')]}
    for name in ('joints','stiffness_joints','nearby_joints'):
        data[name] = [r.array('ii') for _ in range(r.count())]
    for name, fmt in [('neighbors','i'),('neighbor_counts','i'),('blend','f'),('strength','f')]:
        data[name] = [x[0] for x in r.array(fmt)]
    require(len(data['triangles']) % 3 == 0, 'triangle_count', 'Cloth indices')
    for i in data['triangles']+data['physics_to_mesh']:
        require(0 <= i < len(data['mesh_to_physics']), 'mesh_index', str(i))
    for i in data['mesh_to_physics']+data['neighbors']:
        require(0 <= i < len(data['particles']), 'particle_index', str(i))
    counts = data['neighbor_counts']
    require(len(counts) == len(data['particles'])+1 and counts[0] == 0 and counts[-1] == len(data['neighbors'])
            and counts == sorted(counts), 'neighbor_offsets', 'Invalid CSR offsets')
    require(len(data['blend']) == len(data['strength']) == len(data['mesh_to_physics']), 'cloth_count', 'Blend/strength mismatch')
    for name in ('joints','stiffness_joints','nearby_joints'):
        for group in data[name]:
            for pair in group:
                require(all(0 <= i < len(data['particles']) for i in pair), 'particle_index', str(pair))
    return data


def hair_record(r):
    version = r.header('RuntimeHairGeometryCreator', ('1.0','1.1'))
    data = {'version': version, 'scalp': r.string(), 'segments': r.integer(), 'segment_length': r.number(), 'mask_name': r.string()}
    require(1 <= data['segments'] <= 1024, 'hair_segments', str(data['segments']))
    data['mask'] = [r.boolean() for _ in range(r.count())]
    data['strands'] = [{'scalp_index': r.integer(), 'vertices': r.array('fff')} for _ in range(r.count())]
    data['indices'] = [x[0] for x in r.array('i')]
    data['vertices'] = r.array('fff')
    data['rigidities'] = [x[0] for x in r.array('f')] if version == '1.1' else []
    data['root_to_scalp'] = [x[0] for x in r.array('i')]
    data['nearby_groups'] = [r.array('ffff') for _ in range(r.count())]
    require(len(data['vertices']) % data['segments'] == 0, 'hair_vertex_count', 'Incomplete strand')
    require(not data['rigidities'] or len(data['rigidities']) == len(data['vertices']), 'hair_rigidity_count', 'Mismatch')
    for i in data['indices']:
        require(0 <= i < len(data['vertices']), 'hair_index', str(i))
    for i in data['root_to_scalp']:
        require(0 <= i < len(data['strands']), 'scalp_index', str(i))
    for strand in data['strands']:
        require(0 <= strand['scalp_index'] < len(data['strands']), 'scalp_index', str(strand['scalp_index']))
    return data


def decode_vab(data, vam, vaj):
    r = Reader(data); r.header('DynamicStore')
    components, meshes, wraps, material_options = vaj.get('components'), [], [], []
    require(isinstance(components, list), 'invalid_vaj', 'components must be a list')
    for component in components:
        name = component.get('type')
        if name == 'DAZMesh': meshes.append(mesh_record(r))
        elif name == 'DAZSkinWrap':
            r.header(name); wrap = {'name': r.string()}; r.header('DAZSkinWrapStore')
            wrap['vertices'] = r.array('iiiiffffff'); wraps.append(wrap)
        elif name in ('DAZSkinWrapMaterialOptions',):
            r.header('MaterialOptions')
            material_options.append({'id': r.string(), 'slots': [x[0] for x in r.array('i')]})
        else: raise DecodeError('unknown_component', str(name), r.pos)
    dynamic = None
    if r.boolean():
        kind = vam.get('itemType','')
        if kind.startswith('Clothing'): dynamic = {'cloth': cloth_record(r)}
        elif kind.startswith('Hair'): dynamic = {'hair': hair_record(r)}
        else: raise DecodeError('unknown_item_type', kind, r.pos)
    compatibility=[]
    if r.pos != len(data) and vam.get('itemType') in ('HairFemale','HairMale'):
        from vam_plan import is_hair_credit_trailer
        trailer=data[r.pos:].decode('utf-8',errors='replace')
        if is_hair_credit_trailer(trailer) and trailer.encode('utf-8') == data[r.pos:]:
            compatibility.append({'code':'vam_credit_trailer','byte_offset':r.pos,
                                  'raw_trailer':trailer,'message':'Recognized trailing hair credits after complete DynamicStore payload'})
            r.pos=len(data)
    r.end()
    for mesh in meshes: validate_mesh(mesh)
    for options in material_options:
        require(len(meshes)==1 and all(0<=v<len(meshes[0]['materials']) for v in options['slots']), 'material_index', str(options['slots']))
    if dynamic and 'cloth' in dynamic:
        cloth=dynamic['cloth']
        require(len(meshes)==1 and len(cloth['mesh_to_physics'])==len(meshes[0]['uv']), 'cloth_count', 'Cloth render vertex domain mismatch')
        require(len(cloth['physics_to_mesh'])==len(cloth['particles']), 'cloth_count', 'Physics map mismatch')
    for wrap in wraps:
        require(len(meshes) == 1 and len(wrap['vertices']) == len(meshes[0]['uv']), 'wrap_count', 'Wrap/UV vertices mismatch')
        for v in wrap['vertices']:
            require(all(i >= 0 for i in v[:4]), 'wrap_index', str(v[:4]))
    return {'layout': 'DynamicStore/1.0', 'meshes': meshes, 'wraps': wraps, 'dynamic': dynamic,
            'material_options': material_options, 'vam': vam, 'vaj': vaj, 'sha256': hashlib.sha256(data).hexdigest(),
            **({'metadata_compatibility':compatibility} if compatibility else {})}


def validate_mesh(m):
    finite(m)
    n, nuv, nm = len(m['vertices']), len(m['uv']), len(m['materials'])
    require(0 < n <= nuv <= MAX_COUNT, 'vertex_count', f'base={n}, uv={nuv}')
    require(len(m['polygons']) == len(m['uv_polygons']), 'polygon_count', 'Base/UV mismatch')
    mapping = list(range(n)) + [-1]*(nuv-n)
    for v in m['uv_map']:
        a,b = v['fromvert'],v['tovert']
        require(0 <= a < n and 0 <= b < nuv, 'uv_map_index', str(v))
        require(0 <= v['polyindex'] < len(m['polygons']), 'polygon_index', str(v))
        require(mapping[b] in (-1,b,a), 'uv_map_conflict', str(v))
        mapping[b] = a
    for p, up in zip(m['polygons'],m['uv_polygons']):
        require(0 <= p['materialNum'] < nm and p['materialNum'] == up['materialNum'], 'material_index', str(p['materialNum']))
        require(len(p['vertices']) in (3,4) and len(p['vertices']) == len(up['vertices']), 'polygon_layout', 'Expected matching triangles/quads')
        for i,j in zip(p['vertices'],up['vertices']):
            require(0 <= i < n and 0 <= j < nuv, 'vertex_index', str((i,j)))
            require(mapping[j] == i, 'uv_map_mismatch', str((i,j,mapping[j])))
    require(all(i >= 0 for i in mapping), 'uv_map_missing', 'Unmapped UV vertices')
    return mapping


def preview_mesh(m, label, locator):
    mapping = validate_mesh(m)
    vertices = [to_ue(m['vertices'][i]) for i in mapping]
    sections = [[] for _ in m['materials']]; poly_map = [[] for _ in m['materials']]
    for index,p in enumerate(m['uv_polygons']):
        a,b,c,*tail = p['vertices']; triangles = [c,b,a]
        if tail: triangles += [a,tail[0],c]
        sections[p['materialNum']].extend(triangles)
        poly_map[p['materialNum']].extend([index]*(len(triangles)//3))
    return {'name': label, 'locator': locator, 'vertices': vertices, 'uv': [uv_to_ue(v) for v in m['uv']],
            'sections': sections, 'materials': m['materials'], 'converted_to_source_vertex': mapping,
            'triangle_to_source_polygon': poly_map}
