"""Typed Unity source adapter, gated by researched type-tree signatures."""
import hashlib
import json
from pathlib import Path
import struct
from vam_decode import DecodeError, require, finite, vec, validate_mesh


def schema_hash(node):
    def fields(n):
        return [n.m_Name, n.m_Type, n.m_ByteSize, n.m_MetaFlag, [fields(c) for c in n.m_Children]]
    return hashlib.sha256(json.dumps(fields(node), separators=(',', ':')).encode()).hexdigest()


class Bundle:
    def __init__(self, path):
        import UnityPy
        self.path = Path(path)
        require(self.path.stat().st_size <= 256*1024*1024, 'bundle_limit', str(path))
        self.env = UnityPy.load(str(path))
        self.layouts = json.loads((Path(__file__).parents[1]/'Config/DecodeLayouts.json').read_text(encoding='utf-8'))
        self.cache = {}

    def class_name(self, obj):
        if obj.type.name != 'MonoBehaviour': return obj.type.name
        raw = obj.get_raw_data()
        require(len(raw) >= 28, 'truncated', 'MonoBehaviour header')
        file_id, script_id = struct.unpack_from('<iq', raw, 16)
        require(file_id == 0, 'script_reference', 'External script definition is not supported')
        script = obj.assets_file.objects.get(script_id)
        require(script is not None and script.type.name == 'MonoScript', 'script_reference', str(script_id))
        return script.read().m_ClassName

    def records(self, classes):
        for obj in sorted(self.env.objects, key=lambda x:x.path_id):
            if obj.type.name != 'MonoBehaviour': continue
            name = self.class_name(obj)
            if name not in classes: continue
            yield obj, name, self.read(obj, name)

    def read(self, obj, name):
        require(obj.assets_file.unity_version == '2018.1.9f1', 'unity_version', obj.assets_file.unity_version)
        node = obj.serialized_type.node
        require(node is not None, 'missing_type_tree', name)
        digest = schema_hash(node)
        require(digest in self.layouts.get(name, []), 'unknown_layout', name + ':' + digest)
        if obj.path_id not in self.cache:
            value = obj.read_typetree()
            finite(value, name)
            self.cache[obj.path_id] = value
        return self.cache[obj.path_id]


def mesh_from_unity(data):
    m = {'names': {k:data[k] for k in ('nodeId','sceneNodeId','geometryId','sceneGeometryId')},
         'vertices': [vec(v) for v in data['_baseVertices']], 'uv': [vec(v,2) for v in data['_OrigUV']],
         'materials': data['_materialNames'], 'polygons': data['_basePolyList'],
         'uv_polygons': data['_UVPolyList'], 'uv_map': data['_baseVerticesToUVVertices']}
    require(len(m['vertices']) == data['_numBaseVertices'] and len(m['uv']) == data['_numUVVertices'], 'vertex_count', 'Serialized count mismatch')
    require(len(m['materials']) == data['_numMaterials'] and len(m['polygons']) == data['_numBasePolygons'], 'material_count', 'Serialized count mismatch')
    validate_mesh(m)
    return m


def builtin_scalp(person, shared, name):
    """Resolve the actual CustomHair prefab's named DAZ skin binding."""
    candidates=[]
    fields=('closestTriangle','Vertex1','Vertex2','Vertex3','surfaceNormalProjection','surfaceTangent1Projection','surfaceTangent2Projection','surfaceNormalWrapNormalDot','surfaceTangent1WrapNormalDot','surfaceTangent2WrapNormalDot')
    for obj,cls,data in person.records({'DAZSkinWrap'}):
        objects=obj.assets_file.objects
        go=objects[data['m_GameObject']['m_PathID']].read()
        if go.m_Name!=name:continue
        mesh_ref=data['dazMesh'];store_ref=data['wrapStore']
        require(mesh_ref['m_FileID']==0,'scalp_reference','External scalp mesh')
        mesh=mesh_from_unity(person.read(objects[mesh_ref['m_PathID']],'DAZMesh'))
        store_bundle=person if store_ref['m_FileID']==0 else shared
        if store_ref['m_FileID']:
            external=obj.assets_file.externals[store_ref['m_FileID']-1].path.rsplit('/',1)[-1].lower()
            require(any(a.name.lower()==external for a in shared.env.assets),'scalp_reference','Unexpected wrap-store bundle')
        store=next((o for o in store_bundle.env.objects if o.path_id==store_ref['m_PathID']),None)
        require(store is not None,'scalp_reference','Missing wrap store')
        raw=store_bundle.read(store,'DAZSkinWrapStore')
        wrap={'name':name,'vertices':[[v[k] for k in fields]for v in raw['wrapVertices']]}
        candidates.append((mesh,wrap))
    require(candidates,'scalp_missing',name)
    # CustomHair and CustomHairCreator duplicate the same scalp. Never resolve
    # different geometry merely by name or by a guessed vertex count.
    require(all(pair==candidates[0]for pair in candidates),'scalp_ambiguous',name)
    return candidates[0]


def skeleton(bundle, gender):
    bones = {o.path_id:d for o,c,d in bundle.records({'DAZBone'})}
    # The character skeleton has named DAZ body bones; hair rigs form other groups.
    groups = {}
    for oid,d in bones.items(): groups.setdefault(d['dazBones']['m_PathID'],{})[oid] = d
    candidates = [g for g in groups.values() if {'hip','head','lHand','rHand','lFoot','rFoot'} <= {b['_id'] for b in g.values()}]
    require(len(candidates) == 1, 'skeleton_ambiguous', 'Need one complete DAZ body hierarchy')
    group = candidates[0]; result=[]
    for oid,d in sorted(group.items(), key=lambda x:x[1]['_id']):
        parent = d['parentBone']['m_PathID']
        require(parent == 0 or parent in group, 'bone_reference', str(parent))
        seen = {oid}; ancestor = parent
        while ancestor:
            require(ancestor not in seen, 'bone_cycle', d['_id']); seen.add(ancestor)
            ancestor = group[ancestor]['parentBone']['m_PathID']
        result.append({'name': d['_id'], 'parent': group[parent]['_id'] if parent else None,
                       'source_object': str(oid), 'position': vec(d['_maleWorldPosition' if gender=='male' else '_worldPosition']),
                       'orientation_degrees': vec(d['_maleWorldOrientation' if gender=='male' else '_worldOrientation']),
                       'rotation_order': d['_maleRotationOrder' if gender=='male' else '_rotationOrder'], 'parameters': d})
    require(len({b['name'] for b in result}) == len(result), 'duplicate_bone', 'Duplicate bone names')
    return result


def validate_skin(data, mesh, bones):
    require(data['_numBones'] == len(data['nodes']), 'bone_count', 'Skin node count mismatch')
    names = {b['name'] for b in bones}; n = len(mesh['uv']); totals = [[0.,0.,0.]for _ in range(n)]
    for node in data['nodes']:
        require(node['name'] in names, 'bone_reference', node['name'])
        for row in node['weights']:
            v = row['vertex']; require(0 <= v < n, 'weight_vertex', str(v))
            for axis,key in enumerate(('xweight','yweight','zweight')):
                weight = row[key]; require(0 <= weight <= 1.00001, 'weight_range', str(weight))
                totals[v][axis] += weight
        for row in node['generalWeights']:
            require(0 <= row['vertex'] < n, 'weight_vertex', str(row))
            require(0 <= row['weight'] <= 1.00001, 'weight_range', str(row))
        for v in node['fullyWeightedVertices']:
            require(0 <= v < n, 'weight_vertex', str(v))
    finite(data)
    return {'bones':len(data['nodes']), 'vertex_domain':'source_uv_vertices', 'weight_mode': 'general' if data['_hasGeneralWeights'] else 'triaxial',
            'triaxial_sum_range': [[min(v[a] for v in totals),max(v[a] for v in totals)]for a in range(3)],
            'policy':'Raw axis weights and bulges preserved. No silent normalization or LBS conversion.'}
