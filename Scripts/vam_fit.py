"""Source-evidenced static Morph and skin-wrap operations, without simulation."""
import math
from vam_decode import require, finite, validate_mesh


def apply_morph(body, decoded, value, base_count, uv_count, vertex_offset=0):
    # DAZMorphBank applies in its connected (unmerged) mesh's UV domain.
    # DAZMesh subsequently overwrites duplicate UV vertices from base vertices.
    report={'outside_uv':0,'uv_duplicates':0,'applied_deltas':0};updates={}
    for index,x,y,z in decoded['deltas']:
        require(index>=0,'vertex_index',str(index))
        if index>=uv_count: report['outside_uv']+=1;continue
        if index>=base_count: report['uv_duplicates']+=1;continue
        index+=vertex_offset
        require(index<len(body['vertices']),'morph_domain','Mapped Morph vertex exceeds merged mesh')
        vertex=updates.setdefault(index,body['vertices'][index][:])
        for axis,delta in enumerate((x,y,z)):vertex[axis]+=delta*value
        report['applied_deltas']+=1
    finite(updates)
    for index,vertex in updates.items():body['vertices'][index]=vertex
    return report


def apply_bone_centers(bones, decoded, value):
    names={b['name']:b for b in bones};offsets={};unresolved=set()
    for formula in decoded['parameters'].get('formulas',[]):
        kind=formula.get('targetType')
        # DAZMorphFormulaTargetType from the installed VaM Assembly-CSharp.
        kinds=('MorphValue','BoneCenterX','BoneCenterY','BoneCenterZ','OrientationX','OrientationY','OrientationZ','GeneralScale','ScaleX','ScaleY','ScaleZ','MCM','MCMMult','RotationX','RotationY','RotationZ')
        if isinstance(kind,int) and 0<=kind<len(kinds):kind=kinds[kind]
        if kind in ('BoneCenterX','BoneCenterY','BoneCenterZ') and formula.get('target') in names:
            multiplier=float(formula['multiplier']);finite(multiplier)
            offsets.setdefault(formula['target'],[0.,0.,0.])['XYZ'.index(kind[-1])]=multiplier*value
        else: unresolved.add(str(kind))
    # SetBone*Offset replaces the same axis for one Morph, then sums across Morphs.
    for name,offset in offsets.items():
        b=names[name];b.setdefault('morph_center_offset',[0.,0.,0.])
        for axis in range(3):b['morph_center_offset'][axis]+=offset[axis]
    return unresolved


def finalize_bone_centers(bones):
    objects={b['source_object']:b for b in bones}
    for b in bones:
        offset=list(b.get('morph_center_offset',[0.,0.,0.]))
        parent=str(b.get('parameters',{}).get('parentForMorphOffsets',{}).get('m_PathID',0))
        if parent in objects:
            other=objects[parent].get('morph_center_offset',[0.,0.,0.])
            offset=[a+c for a,c in zip(offset,other)]
        b['base_position']=b['position'][:]
        b['position']=[a+c for a,c in zip(b['position'],offset)]


def triangles(mesh):
    out=[]
    # Unity Mesh.triangles concatenates material submeshes in slot order.
    for p in sorted(mesh['polygons'],key=lambda p:p['materialNum']):
        v=p['vertices'];out.append([v[2],v[1],v[0]])
        if len(v)==4:out.append([v[0],v[3],v[2]])
    return out


def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]


def fit_wrap(mesh, wrap, target):
    """DAZSkinWrap.Wrap CPU basis with zero extra thickness and no smoothing."""
    verts=target['vertices'];tris=triangles(target);out=[]
    require(len(wrap['vertices'])==len(mesh['uv']),'wrap_count','Expected UV-domain wrap records')
    for row in wrap['vertices'][:len(mesh['vertices'])]:
        t,a,b,c,nproj,t1proj,t2proj,*_=row
        require(0<=t<len(tris) and all(0<=i<len(verts) for i in (a,b,c)), 'wrap_target_index', str(row[:4]))
        require(set(tris[t])=={a,b,c},'wrap_topology','Binding does not match target triangle')
        x,y,z=[verts[i] for i in tris[t]]
        normal=cross([y[i]-x[i] for i in range(3)],[z[i]-x[i] for i in range(3)])
        length=math.sqrt(sum(v*v for v in normal))
        require(length>1e-15,'wrap_degenerate','Target triangle has no normal')
        normal=[v/length for v in normal]
        origin=verts[a];tangent=[(verts[a][i]+verts[b][i]+verts[c][i])*.33333-origin[i] for i in range(3)]
        bitangent=cross(tangent,normal)
        out.append([origin[i]+tangent[i]*t1proj+bitangent[i]*t2proj+normal[i]*nproj for i in range(3)])
    finite(out)
    return dict(mesh,vertices=out)
