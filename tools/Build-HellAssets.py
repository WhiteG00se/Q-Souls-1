"""Developer-only arena recipe builder. Game-derived output stays in --temporary.
Requires soulstruct 2.3.2, soulstruct-havok 1.2.2, and shapely 2.1.2 under --libraries.
Players use the launcher; Python and these libraries are not distributed.
"""
import argparse
import base64
import copy
import hashlib
import json
import math
import re
import os
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game', default=r'D:/Games/Steam/steamapps/common/DARK SOULS REMASTERED')
parser.add_argument('--libraries', default=r'D:/AI/Tools/Python/qs1-map')
parser.add_argument('--temporary', default=r'D:/AI/Temp/Q-Souls-1/hell')
parser.add_argument('--recipes', default=str(Path(__file__).resolve().parents[1] / 'assets/hell'))
args = parser.parse_args()
sys.path.insert(0, args.libraries)
sys.dont_write_bytecode = True
os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
import soulstruct.logging_utils as _logging
_setup_log = _logging.setup
_logging.setup = lambda **kw: _setup_log(**(kw | {"log_path": Path(args.temporary) / "soulstruct-build.log"}))
import numpy as np
from soulstruct.containers import Binder, BinderEntry, TPF
from AreaGeometry import RADIUS, BONFIRE_LOCAL, ARRIVAL_LOCAL, LAVA_Y, DROP_DISTANCE, island, lava, boulder, barrier
from soulstruct.darksouls1r.maps import MSB
from soulstruct.darksouls1r.models import FLVER
from soulstruct.darksouls1r.events import EMEVD
from soulstruct.darksouls1r.params import DrawParamBND
from soulstruct.base.maps.navmesh.nvm import NVM, NVMTriangle
from soulstruct.base.maps.navmesh.mcp import MCP, NavmeshAABB
from soulstruct.base.maps.navmesh.mcg import MCG, MCGNode, MCGEdge
from soulstruct.dcx import DCXType
from soulstruct.flver.face_set import FaceSet
from soulstruct.utilities.maths import Vector3, EulerDeg
from soulstruct.base.maps.msb.utils import BitSet128
from soulstruct.havok.core import HKX
from soulstruct.havok.fromsoft.shared import map_collision as collision_module
from soulstruct.havok.fromsoft.shared.map_collision import MapCollisionModel, MapCollisionModelMesh

GAME = Path(args.game).resolve()
TEMP = Path(args.temporary).resolve()
RECIPES = Path(args.recipes).resolve()
for output in (TEMP, RECIPES):
    if output == GAME or GAME in output.parents:
        raise ValueError('Generated files must stay outside the game directory.')
    output.mkdir(parents=True, exist_ok=True)
MAP = 'm14_01_00_00'
CENTRE = np.array([1500., -380., 100.])
BONFIRE, ACTOR, SPAWN, ARRIVAL = 1411965, 1410965, 1412965, 1410971
FIRELINK_ARRIVAL = 1020971
# Native EzState bonfire lookup rejects object-ID suffixes outside 1950..1969.
assert 1950 <= BONFIRE % 10000 <= 1969
assert ACTOR + 1000 == BONFIRE and SPAWN - 1000 == BONFIRE
BONFIRE_FLAG = 11410944
TO_HELL, TO_FIRELINK = 11025950, 11415950
SEGMENTS = 128


def unpack(b):
    return zlib.decompress(b[76:]) if b[:4] == b'DCX\0' else b


def source(path, offset=0, length=0, inflate=False):
    raw = (GAME / path).read_bytes()
    data = raw[offset:offset + length] if length else raw
    if inflate:
        data = unpack(data)
    return dict(path=path, offset=offset, length=length, inflate=inflate, data=data)


def recipe(name, sources, data, compression=-1):
    """Only copy references into installed assets plus mod-authored changes.
    Never store compressed game files or complete extracted assets in Git.
    """
    root = ET.Element('hell', version='1', size=str(len(data)), sha256=hashlib.sha256(data).hexdigest(), compression=str(compression))
    for item in sources:
        ET.SubElement(root, 'source', path=item['path'], offset=str(item['offset']), length=str(item['length']),
                      inflate=str(item['inflate']).lower(), sha256=hashlib.sha256(item['data']).hexdigest())
    # Index overlapping 16-byte runs. Limit duplicate candidates to bound work on padding.
    index = {}
    for si, item in enumerate(sources):
        b = item['data']
        for p in range(0, len(b) - 15, 16 if len(b) > 1024 * 1024 else 1):
            key = b[p:p + 16]
            candidates = index.setdefault(key, [])
            if len(candidates) < 4:
                candidates.append((si, p))
    position, pending = 0, bytearray()
    def flush():
        if pending:
            ET.SubElement(root, 'bytes').text = base64.b64encode(pending).decode('ascii')
            pending.clear()
    while position < len(data):
        best = (0, 0, 0)
        for si, start in index.get(data[position:position + 16], []):
            b = sources[si]['data']
            limit = min(len(b) - start, len(data) - position)
            n = 16
            while n + 64 <= limit and b[start + n:start + n + 64] == data[position + n:position + n + 64]:
                n += 64
            while n < limit and b[start + n] == data[position + n]:
                n += 1
            if n > best[0]:
                best = (n, si, start)
        if best[0] >= 16:
            flush()
            n, si, start = best
            ET.SubElement(root, 'copy', source=str(si), offset=str(start), length=str(n))
            position += n
        else:
            pending.append(data[position]); position += 1
    flush()
    ET.indent(root)
    ET.ElementTree(root).write(RECIPES / (name + '.xml'), encoding='utf-8', xml_declaration=True)
    (TEMP / (name + '.generated')).write_bytes(data)
    # Independent reconstruction catches builder indexing errors.
    result = bytearray()
    for e in root:
        if e.tag == 'copy':
            b = sources[int(e.attrib['source'])]['data']; off = int(e.attrib['offset'])
            result.extend(b[off:off + int(e.attrib['length'])])
        elif e.tag == 'bytes':
            result.extend(base64.b64decode(e.text))
    assert result == data
    print(name, len(data), 'bytes;', (RECIPES / (name + '.xml')).stat().st_size, 'recipe bytes')


def next_instance_name(seq, model):
    # Stay within the native sequence instead of introducing a sparse instance ID.
    ids = [int(e.name[-4:]) for e in seq if re.fullmatch(model + r'_\d{4}', e.name)]
    return f'{model}_{max(ids, default=-1) + 1:04d}'


def append_clone(seq, original, **values):
    entry = original.copy()
    for key, value in values.items():
        setattr(entry, key, value)
    assert entry.name not in [e.name for e in seq]
    if getattr(entry, 'entity_id', -1) > 0:
        assert entry.entity_id not in [getattr(e, 'entity_id', -1) for e in seq]
    seq.append(entry)
    return entry


# The fighting circle stays flat; only the short spur rises to the bonfire.
vertices, triangles, walk_triangles, footprint = island()
# Reuse the existing ground and animated lava shaders and texture bindings verbatim.
base_file = f'map/{MAP}/m0170B1A14.flver.dcx'
lava_file = f'map/{MAP}/m0010B1A14.flver.dcx'
f = FLVER.from_path(GAME / base_file)
f.debone_map_piece()
ground_template = copy.deepcopy(f.meshes[0])
lava_flver = FLVER.from_path(GAME / lava_file)
lava_flver.debone_map_piece()
lava_template = next(mesh for mesh in lava_flver.meshes if 'A14_lava_M_02' in mesh.material.mat_def_path)


def mesh_from(template, verts, faces, cylindrical=False):
    mesh = copy.deepcopy(template)
    # Split at faces for rock facets and wall UVs, rather than stretching the
    # floor's horizontal UV projection vertically up a paper-thin border.
    positions = verts[faces].reshape(-1, 3)
    a = np.repeat(mesh.vertex_arrays[0].array[:1], len(positions), axis=0)
    a['position'] = positions
    a['bone_indices'] = 0
    normal = np.cross(verts[faces[:, 1]] - verts[faces[:, 0]], verts[faces[:, 2]] - verts[faces[:, 0]])
    normal /= np.maximum(np.linalg.norm(normal, axis=1, keepdims=True), 1e-8)
    a['normal'] = np.repeat(normal, 3, axis=0)
    # Preserve native NormalW packing (127), including for non-skinned meshes.
    a['normal_w'] = template.vertex_arrays[0].array['normal_w'][0]
    tangents = np.empty((len(faces), 4),dtype=np.float32)
    uv = np.empty((len(faces), 3, 2), dtype=np.float32)
    for i,n in enumerate(normal):
        p=verts[faces[i]]
        if cylindrical:
            theta=np.arctan2(p[:,2],p[:,0])
            if theta.max()-theta.min()>math.pi:theta[theta<0]+=math.tau
            uv[i]=np.column_stack([theta*RADIUS/3.,-p[:,1]/3.])
        else:
            axis=int(np.abs(n).argmax())
            axes=[0,2] if axis==1 else ([2,1] if axis==0 else [0,1])
            uv[i]=p[:,axes]/3.
        e1,e2=p[1]-p[0],p[2]-p[0]
        d1,d2=uv[i,1]-uv[i,0],uv[i,2]-uv[i,0]
        det=d1[0]*d2[1]-d1[1]*d2[0]
        assert abs(det)>1e-10
        t=(e1*d2[1]-e2*d1[1])/det
        bitangent=(e2*d1[0]-e1*d2[0])/det
        t-=n*np.dot(n,t);t/=max(np.linalg.norm(t),1e-8)
        tangents[i,:3]=t
        tangents[i,3]=-1. if np.dot(np.cross(n,t),bitangent)<0 else 1.
    a['tangent_0'] = np.repeat(tangents, 3, axis=0)
    a['color_0'] = [1, 1, 1, 1]
    a['uv_0'] = uv.reshape(-1, 2)
    mesh.vertex_arrays[0].array = a
    mesh.vertex_arrays = mesh.vertex_arrays[:1]
    mesh.default_bone_index = 0
    mesh.bone_indices = np.array([0], dtype=np.int32)
    # DS1 FLVER front faces are CLOCKWISE relative to their outward normals.
    # Havok triangles above retain their working winding; reverse only rendering.
    indices = np.arange(len(positions), dtype=np.uint32).reshape(-1, 3)[:, [0, 2, 1]]
    mesh.face_sets = [FaceSet(flags=0, is_triangle_strip=False, use_backface_culling=True, unk_x06=0, vertex_indices=indices)]
    return mesh

lava_vertices,lava_faces=lava(footprint)
fall_vertices,fall_faces=lava(footprint,falling=True)
flat_lava=mesh_from(lava_template,lava_vertices,lava_faces)
fall=mesh_from(lava_template,fall_vertices,fall_faces,cylindrical=True)
fall_array=fall.vertex_arrays[0].array
fade=np.clip((fall_array['position'][:,1]+28.)/(LAVA_Y+28.),0.,1.)**2
fall_array['color_0']=np.repeat(fade[:,None],4,axis=1)
f.meshes=[mesh_from(ground_template,vertices,triangles),flat_lava,fall]
# The former outcrop was only four vertices/two triangles. Keep its native
# rock material, replace those flat strips with genuinely closed boulders.
rock_file=f'map/{MAP}/m0100B1A14.flver.dcx'
rock=FLVER.from_path(GAME/rock_file);rock.debone_map_piece()
for i in range(10):
    rock_vertices,rock_faces=boulder(i)
    f.meshes.append(mesh_from(rock.meshes[0],rock_vertices,rock_faces))
f.dummies = []
f.dcx_type = DCXType.Null
f.refresh_bounding_boxes(); f.refresh_bone_bounding_boxes()
flver_data = bytes(f)
check = FLVER.from_bytes(flver_data)
assert len(check.meshes) == 13
recipe('terrain', [source(base_file, inflate=True), source(lava_file, inflate=True), source(rock_file, inflate=True)], flver_data, 0)

# Reuse Darkroot's actual outdoor night sky and its existing texture, loaded
# into Izalith's common texture pool without modifying any installed game file.
sky_file = 'map/m12_00_00_00/m9000B0A12.flver.dcx'
sky = FLVER.from_path(GAME / sky_file)
sky.debone_map_piece()
for mesh in sky.meshes:
    va=mesh.vertex_arrays[0].array
    va['position']*=.45
    original_faces=mesh.face_sets[0].triangulate(True)
    clipped=[]
    for face in original_faces:
        polygon=[va[i].copy() for i in face]
        result=[]
        for current,previous in zip(polygon,polygon[-1:]+polygon[:-1]):
            inside=current['position'][1]>=LAVA_Y
            before=previous['position'][1]>=LAVA_Y
            if inside!=before:
                t=(LAVA_Y-previous['position'][1])/(current['position'][1]-previous['position'][1])
                v=previous.copy()
                for name in va.dtype.names:
                    if name.startswith(('position','normal','tangent','color','uv')) and name!='normal_w':
                        v[name]=previous[name]*(1-t)+current[name]*t
                result.append(v)
            if inside:result.append(current)
        for i in range(1,len(result)-1):clipped.extend([result[0],result[i],result[i+1]])
    mesh.vertex_arrays[0].array=np.array(clipped,dtype=va.dtype)
    mesh.face_sets=[FaceSet(flags=0,is_triangle_strip=False,use_backface_culling=True,unk_x06=0,
                            vertex_indices=np.arange(len(clipped),dtype=np.uint32).reshape(-1,3))]
sky.dcx_type = DCXType.Null
sky.refresh_bounding_boxes(); sky.refresh_bone_bounding_boxes()
recipe('sky', [source(sky_file, inflate=True)], bytes(sky), 0)
texture_file = 'map/m14/m14_9999.tpf.dcx'
night_textures = 'map/m12/m12_9999.tpf.dcx'
textures = TPF.from_path(GAME / texture_file)
night = TPF.from_path(GAME / night_textures)
assert 'm12_sky' not in {t.stem for t in textures.textures}
textures.textures.append(copy.deepcopy(next(t for t in night.textures if t.stem == 'm12_sky')))
textures.dcx_type = DCXType.Null
texture_data = bytearray(bytes(textures))
for i, texture in enumerate(textures.textures):
    assert texture.unknown_float_struct is None
    # Preserve native PC texture metadata; the library otherwise normalizes mip counts.
    texture_data[16 + i * 20 + 9] = int(texture.texture_type)
    texture_data[16 + i * 20 + 10] = texture.mipmap_count
recipe('sky-textures', [source(texture_file, inflate=True), source(night_textures, inflate=True)], bytes(texture_data), 0)

# Bind an actual outdoor GI probe for metallic surfaces. The missing probe
# previously left reflective weapons black despite readable diffuse lighting.
# Native resource lookup reads the environment entity ID, not its bake-lighting row.
PROBE_ID=8
probe_header='map/m10/GI_EnvM_m10.tpfbhd'
probe_data='map/m10/GI_EnvM_m10.tpfbdt'
probe_binder=Binder.from_path(GAME/probe_header)
probe_entry=next(e for e in probe_binder.entries if e.name=='GI_Env_m10_02_0000.tpf.dcx')
h=(GAME/probe_header).read_bytes()
record=next(32+i*24 for i in range(struct.unpack_from('<i',h,16)[0]) if struct.unpack_from('<i',h,32+i*24+12)[0]==probe_entry.entry_id)
probe_size,probe_offset=struct.unpack_from('<ii',h,record+4)
probe=TPF.from_bytes(probe_entry.data)
for texture in probe.textures:texture.stem=texture.stem.replace('m10_02_0000','m14_01_0008')
probe.dcx_type=DCXType.Null
probe_bytes=bytearray(bytes(probe))
for i,texture in enumerate(probe.textures):
    assert texture.unknown_float_struct is None
    probe_bytes[16+i*20+9]=int(texture.texture_type)
    probe_bytes[16+i*20+10]=texture.mipmap_count
recipe('outdoor-probe',[source(probe_data,probe_offset,probe_size,True)],bytes(probe_bytes),0)

# A dedicated outdoor lighting row affects only the isolated pocket. Reuse
# Firelink's readable ambient light, warm it from below, and retain night fog.
lighting_file = 'param/DrawParam/a14_DrawParam.parambnd.dcx'
lighting = DrawParamBND.from_path(GAME / lighting_file)
firelink_lighting_file = 'param/DrawParam/a10_DrawParam.parambnd.dcx'
firelink_lighting = DrawParamBND.from_path(GAME / firelink_lighting_file)
LIGHTING_ID = 110
for bank in lighting.draw_params_0:
    dest = lighting.draw_params_0[bank]
    src = firelink_lighting.draw_params_0[bank]
    assert LIGHTING_ID not in dest.rows
    row = copy.deepcopy(src.rows.get(20, src.rows[0]))
    row.Name = 'QS1 Area Outdoors'
    if bank == 'LightBank':
        row.TopDownLightAlpha = 110
        row.BottomUpLightRed, row.BottomUpLightGreen, row.BottomUpLightBlue = 255, 125, 55
        row.BottomUpLightAlpha = 160
        row.BakedLight0Alpha = 130
        row.DiffuseLightAlpha = 260
        row.BakedLight1Alpha = 60
        row.BakedLight2Alpha = 60
        row.SpecularLightAlpha = 200
    elif bank == 'FogBank':
        row.FogStartDistance, row.FogEndDistance = 140, 950
        row.Red, row.Green, row.Blue, row.Alpha = 25, 30, 45, 100
    elif bank == 'ToneMapBank':
        row.OverallBrightness = .22
        row.MinimumAdaptationBrightness, row.MaximumAdaptationBrightness = .09, .22
    dest.rows[LIGHTING_ID] = row
# Repack only the edited table entries, preserving all other binder entries.
lighting_binder = Binder.from_path(GAME / lighting_file)
for entry in lighting_binder.entries:
    bank = entry.name.removeprefix('m14_').removesuffix('.param').replace('s14_', 's_')
    if bank in lighting.draw_params_0:
        entry.set_uncompressed_data(bytes(lighting.draw_params_0[bank]))
lighting_binder.dcx_type = DCXType.Null
recipe('lighting', [source(lighting_file, inflate=True), source(firelink_lighting_file, inflate=True)], bytes(lighting_binder), 0)

# Generate matching high/low resolution collision from exactly the visible earth geometry.
bhd_file = f'map/{MAP}/h14_01_00_00.hkxbhd'
bdt_file = f'map/{MAP}/h14_01_00_00.hkxbdt'
bhd = (GAME / bhd_file).read_bytes()
assert bhd[:4] == b'BHF3' and bhd[12:16] == bytes([0x74, 0, 0, 0])
size, offset = struct.unpack_from('<ii', bhd, 36)
hkx_source = source(bdt_file, offset, size, True)
(TEMP / 'collision-template.hkx').write_bytes(hkx_source['data'])
original_resource_path = collision_module.SOULSTRUCT_HAVOK_PATH
collision_module.SOULSTRUCT_HAVOK_PATH = lambda path: TEMP / 'collision-template.hkx' if path.endswith('MapCollisionTemplate_DSR.hkx') else original_resource_path(path)


def mopper_here(lines, mode):
    input_path = TEMP / 'mopper-input.txt'
    output_path = TEMP / 'mopp.json'
    input_path.write_text(re.sub(r'np\.float32\(([^)]+)\)', r'\1', '\n'.join(lines)) + '\n')
    if output_path.exists():
        output_path.unlink()
    subprocess.run([str(Path(args.libraries) / 'soulstruct/havok/resources/mopper.exe'), mode, str(input_path)],
                   cwd=TEMP, stdout=subprocess.PIPE, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    return json.loads(output_path.read_text())


collision_module.mopper = mopper_here
verts4 = np.column_stack([vertices, np.zeros(len(vertices), dtype=np.float32)])
faces4 = np.column_stack([triangles, np.zeros(len(triangles), dtype=np.uint32)]).astype(np.uint16)
barrier_v,barrier_f=barrier(footprint)
barrier_v4=np.column_stack([barrier_v,np.zeros(len(barrier_v),dtype=np.float32)])
barrier_f4=np.column_stack([barrier_f,np.zeros(len(barrier_f),dtype=np.uint32)]).astype(np.uint16)
model = MapCollisionModel(name='h9900B1', meshes=[MapCollisionModelMesh(verts4, faces4, 3),MapCollisionModelMesh(barrier_v4,barrier_f4,3)])
hkx = model.to_hkx()
hkx.dcx_type = DCXType.Null
hkx_data = bytes(hkx)
collision_check = MapCollisionModel.from_hkx(HKX.from_bytes(hkx_data))
assert np.allclose(collision_check.meshes[0].vertices, verts4)
assert np.array_equal(collision_check.meshes[0].faces[:, :3], triangles)
recipe('collision', [hkx_source], hkx_data)

# Preserve existing entries and references; append only the isolated pocket.
m = MSB.from_path(GAME / f'map/MapStudio/{MAP}.msb')
original_parts = len(m.get_parts())
used_groups = set().union(*(c.display_groups.enabled_bits for c in m.collisions))
group = next(g for g in range(127, 95, -1) if g not in used_groups)
assert group not in used_groups
entity_ids = {getattr(e, 'entity_id', -1) for e in [*m.get_parts(), *m.get_regions(), *m.get_events()]}
assert not entity_ids.intersection({BONFIRE, ACTOR, SPAWN, ARRIVAL})
assert 'm9900B1' not in [x.name for x in m.map_piece_models]
cm = append_clone(m.collision_models, m.collision_models[0], name='h9900B1',
                  sib_path=m.collision_models[0].sib_path.replace(m.collision_models[0].name, 'h9900B1'))
col = append_clone(m.collisions, next(c for c in m.collisions if c.name == 'h1029B1'),
                   name='h9900B1', model=cm, translate=Vector3(CENTRE), rotate=EulerDeg.zero(),
                   entity_id=-1, draw_groups=BitSet128({group}), display_groups=BitSet128({group}),
                   navmesh_groups=BitSet128({group}), attached_bonfire=BONFIRE, vagrant_entity_ids=[0, 0, 0],
                   place_name_banner_id=9990, force_place_name_banner=True, environment_event=None, reflect_plane_height=CENTRE[1] + LAVA_Y)
mm = append_clone(m.map_piece_models, m.map_piece_models[0], name='m9900B1',
                  sib_path=m.map_piece_models[0].sib_path.replace(m.map_piece_models[0].name, 'm9900B1'))
part = append_clone(m.map_pieces, m.map_pieces[0], name='m9900B1', model=mm, entity_id=-1,
                    translate=Vector3(CENTRE), rotate=EulerDeg.zero(), scale=Vector3((1., 1., 1.)),
                    draw_groups=BitSet128({group}), display_groups=BitSet128({group}))
# All light banks exist at this new ID; leave all vanilla lighting rows intact.
for field in ['ambient_light_id', 'fog_id', 'scattered_light_id', 'lens_flare_id',
              'shadow_id', 'dof_id', 'tone_map_id', 'point_light_id', 'tone_correction_id']:
    setattr(col, field, LIGHTING_ID)
part.is_shadow_source = False
for field in ['ambient_light_id','fog_id','scattered_light_id','lens_flare_id','shadow_id','dof_id','tone_map_id','point_light_id','tone_correction_id']:
    setattr(part, field, getattr(col, field))
bonfire = append_clone(m.objects, next(o for o in m.objects if o.entity_id == 1411960), name=next_instance_name(m.objects, 'o0200'),
                        entity_id=BONFIRE, translate=Vector3(CENTRE+BONFIRE_LOCAL), rotate=EulerDeg((0.,180.,0.)), draw_parent=col,
                        draw_groups=BitSet128({group}), display_groups=BitSet128({group}))
actor = append_clone(m.characters, next(c for c in m.characters if c.entity_id == 1410960), name=next_instance_name(m.characters, 'c1000'),
                      entity_id=ACTOR, translate=Vector3(CENTRE+BONFIRE_LOCAL+[0.,.5,0.]), rotate=EulerDeg((0.,180.,0.)),
                      talk_id=141020, draw_parent=col, draw_groups=BitSet128({group}), display_groups=BitSet128({group}))
spawn_template = next(s for s in m.spawn_points if s.entity_id == 1412960)
spawn_region = append_clone(m.regions, spawn_template.spawn_point_region, name='QS1_Hell_Respawn',
                             entity_id=-1, translate=Vector3(CENTRE+ARRIVAL_LOCAL), rotate=EulerDeg.zero())
append_clone(m.spawn_points, spawn_template, name='QS1_Hell_Respawn', entity_id=SPAWN, spawn_point_region=spawn_region)
append_clone(m.player_starts, m.player_starts[0], name=next_instance_name(m.player_starts, 'c0000'), entity_id=ARRIVAL,
             translate=Vector3(CENTRE+ARRIVAL_LOCAL), rotate=EulerDeg.zero(),
             draw_groups=BitSet128({group}), display_groups=BitSet128({group}))
# Separate sky draw settings preserve the native sky's rendering behavior.
night_map = MSB.from_path(GAME / 'map/MapStudio/m12_00_00_01.msb')
sky_template = next(p for p in night_map.map_pieces if p.model.name == 'm9000B0')
sky_model = append_clone(m.map_piece_models, m.map_piece_models[0], name='m9901B1',
                         sib_path=mm.sib_path.replace('m9900B1', 'm9901B1'))
append_clone(m.map_pieces, sky_template, name='m9901B1', model=sky_model,
             translate=Vector3(CENTRE), rotate=EulerDeg.zero(), entity_id=-1,
             draw_groups=BitSet128({group}), display_groups=BitSet128({group}),
             ambient_light_id=LIGHTING_ID, fog_id=LIGHTING_ID, scattered_light_id=LIGHTING_ID,
             tone_map_id=LIGHTING_ID, is_shadow_source=False, is_shadow_destination=False)
for entry in (bonfire, actor):
    entry.ambient_light_id = LIGHTING_ID
    entry.tone_map_id = LIGHTING_ID

# A private reference uses a reused Firelink outdoor cubemap, leaving all
# existing Izalith probes and their lighting unchanged.
firelink_map=MSB.from_path(GAME/'map/MapStudio/m10_02_00_00.msb')
probe_template=next(c for c in firelink_map.collisions if c.attached_bonfire==1021960).environment_event
assert PROBE_ID not in {e.entity_id for e in m.environments}
probe_region=append_clone(m.regions,probe_template.attached_region,name='QS1_Area_Outdoor_Probe',
                          entity_id=-1,translate=Vector3(CENTRE+[0.,2.,0.]))
probe_event=append_clone(m.environments,probe_template,name='QS1_Area_Outdoor_Probe',
                         entity_id=PROBE_ID,attached_part=col,attached_region=probe_region,unk_x00_x04=LIGHTING_ID)
col.environment_event=probe_event

# Native backread requires navmesh rooms and a connected gate graph, even for
# an empty arena: it controls character/bonfire activation as well as enemy AI.
nvm_file = f'map/{MAP}/{MAP}.nvmbnd.dcx'
nvm_binder = Binder.from_path(GAME / nvm_file)
mcp_file, mcg_file = f'map/{MAP}/{MAP}.mcp', f'map/{MAP}/{MAP}.mcg'
mcp, mcg = MCP.from_path(GAME / mcp_file), MCG.from_path(GAME / mcg_file)
mcg.set_navmesh_references(m.navmeshes)
assert len(mcp.aabbs) == len(m.navmeshes)
navs, models, gate_triangles = [], [], []
next_id = max(e.entry_id for e in nvm_binder.entries) + 1
for half in range(2):
    # Split the actual walkable circle/ramp/landing at Z=0. Both rooms stay
    # isolated from vanilla routes and the positive half contains the bonfire.
    centres=vertices[walk_triangles].mean(1)
    selection=walk_triangles[(centres[:,2]>=0) if half==0 else (centres[:,2]<0)]
    used,inverse=np.unique(selection,return_inverse=True)
    points=vertices[used]
    faces=[tuple(int(j) for j in f) for f in inverse.reshape(-1,3)]
    edge_faces = {}
    for i, face in enumerate(faces):
        for a, b in [(face[0], face[1]), (face[1], face[2]), (face[0], face[2])]:
            edge_faces.setdefault(tuple(sorted((a, b))), []).append(i)
    tris = []
    for i, face in enumerate(faces):
        neighbours = []
        for a, b in [(face[0], face[1]), (face[1], face[2]), (face[0], face[2])]:
            neighbours.append(next((j for j in edge_faces[tuple(sorted((a, b)))] if j != i), -1))
        tris.append(NVMTriangle(face, tuple(neighbours), 0, 0))
    nvm = NVM(vertices=points, triangles=tris)
    model_name = f'n{9900 + half}B1'
    nav_model = append_clone(m.navmesh_models, m.navmesh_models[0], name=model_name,
                             sib_path=m.navmesh_models[0].sib_path.replace(m.navmesh_models[0].name, model_name))
    nav = append_clone(m.navmeshes, m.navmeshes[0], name=model_name, model=nav_model,
                       translate=Vector3(CENTRE), rotate=EulerDeg.zero(), entity_id=-1,
                       navmesh_groups=BitSet128({group}))
    navs.append(nav)
    entry = copy.deepcopy(nvm_binder.entries[0])
    entry.entry_id = next_id + half
    # Native DSR navigation entries include the area suffix, just like map models.
    entry.path = str(Path(entry.path).with_name(model_name + 'A14.nvm'))
    entry.set_uncompressed_data(bytes(nvm))
    nvm_binder.entries.append(entry)
    index = len(mcp.aabbs)
    other = index + 1 if half == 0 else index - 1
    mcp.aabbs.append(NavmeshAABB(map_id=(14, 1, 0, 0),
        aabb_start=Vector3(CENTRE + points.min(0) - [.5, 2., .5]),
        aabb_end=Vector3(CENTRE + points.max(0) + [.5, 2., .5]),
        connected_navmesh_part_indices=[other]))
    centroids=points[np.array(faces)].mean(1)
    gate_triangles.append([int(np.linalg.norm(centroids-[x,0.,0.],axis=1).argmin()) for x in [10.,-10.]])
node_a = MCGNode(translate=Vector3(CENTRE + [10., 0., 0.]), dead_end_navmesh=None)
node_b = MCGNode(translate=Vector3(CENTRE + [-10., 0., 0.]), dead_end_navmesh=None)
for i, nav in enumerate(navs):
    edge = MCGEdge(node_a_triangles=[gate_triangles[i][0]], node_b_triangles=[gate_triangles[i][1]],
                   map_id=(14, 1, 0, 0), cost=30., node_a=node_a, node_b=node_b, navmesh=nav)
    mcg.edges.append(edge)
    node_a.connected_nodes.append(node_b); node_a.connected_edges.append(edge)
    node_b.connected_nodes.append(node_a); node_b.connected_edges.append(edge)
mcg.nodes.extend([node_a, node_b])
mcg.set_navmesh_indices(m.navmeshes)
nvm_binder.dcx_type = DCXType.Null
recipe('navmesh', [source(nvm_file, inflate=True)], bytes(nvm_binder), 0)
recipe('navmesh-rooms', [source(mcp_file)], bytes(mcp))
recipe('navmesh-gates', [source(mcg_file)], bytes(mcg))
assert len(m.get_parts()) == original_parts + 8
# DSR resolves model files through the SIB path, not the MSB model name alone.
# Keep the native path layout and replace only the source model's stem.
assert cm.sib_path and cm.sib_path.endswith('h9900B1.hkxwin')
assert mm.sib_path and mm.sib_path.endswith('m9900B1.sib')
msb_data = bytes(m)
assert len(MSB.from_bytes(msb_data).objects) == len(m.objects)
recipe('izalith-map', [source(f'map/MapStudio/{MAP}.msb')], msb_data)

firelink_path = 'map/MapStudio/m10_02_00_00.msb'
fm = MSB.from_path(GAME / firelink_path)
assert FIRELINK_ARRIVAL not in {getattr(e, 'entity_id', -1) for e in fm.get_parts()}
firelink_spawn = next(s for s in fm.spawn_points if s.entity_id == 1022960).spawn_point_region
append_clone(fm.player_starts, fm.player_starts[0], name=next_instance_name(fm.player_starts, 'c0000'), entity_id=FIRELINK_ARRIVAL,
             translate=firelink_spawn.translate.copy(), rotate=firelink_spawn.rotate.copy())
recipe('firelink-map', [source(firelink_path)], bytes(fm))

# Compile only new events; retain all original event instructions byte-for-byte.
for map_name, event_id, request, obj, destination, start in [
    ('m10_02_00_00', 11025951, TO_HELL, 1021960, (14, 1), ARRIVAL),
    (MAP, 11415951, TO_FIRELINK, BONFIRE, (10, 2), FIRELINK_ARRIVAL),
]:
    path = f'event/{map_name}.emevd.dcx'
    ev = EMEVD.from_path(GAME / path)
    assert event_id not in ev.events
    assert BONFIRE_FLAG not in ev.events
    extra = f'''from soulstruct.darksouls1r.events import *
from soulstruct.darksouls1r.events.instructions import *
from soulstruct.darksouls1r.game_types import *
@ContinueOnRest(0)
def Constructor():
    DisableFlag({request})
    RunEvent({event_id})
'''
    if map_name == MAP:
        extra += f'    RegisterBonfire(bonfire_flag={BONFIRE_FLAG}, obj={BONFIRE})\n'
    extra += f'''
@ContinueOnRest({event_id})
def Travel():
    DisableNetworkSync()
    MAIN.Await(FlagEnabled({request}))
    DisableFlag({request})
    if Client():
        return RESTART
    if HealthRatio(PLAYER) <= 0.0:
        return RESTART
    if EntityBeyondDistance(PLAYER, {obj}, radius=5.0):
        return RESTART
    WaitFrames(2)
    ForceAnimation(PLAYER, 7114)
    Wait(0.7)
    CreateTemporaryVFX(vfx_id=90021, anchor_entity=PLAYER, dummy_id=17, anchor_type=CoordEntityType.Character)
    Wait(1.5)
    if HealthRatio(PLAYER) <= 0.0:
        return RESTART
    WarpToMap(game_map=({destination[0]}, {destination[1]}), player_start={start})
'''
    (TEMP / (map_name + '-hell.evs.py')).write_text(extra)
    new_ev = EMEVD.from_evs_string(extra)
    ev.events[0].instructions.extend(new_ev.events[0].instructions)
    ev.events[event_id] = new_ev.events[event_id]
    ev.dcx_type = DCXType.Null
    data = bytes(ev)
    assert len(EMEVD.from_bytes(data).events) == len(ev.events)
    recipe('firelink-event' if map_name != MAP else 'izalith-event', [source(path, inflate=True)], data, 0)

(TEMP / 'geometry.json').write_text(json.dumps(dict(centre=CENTRE.tolist(), group=group, radius=RADIUS, bonfire_position=BONFIRE_LOCAL.tolist(),
    vertices=vertices.tolist(), triangles=triangles.tolist(), bonfire=BONFIRE, actor=ACTOR, spawn=SPAWN, arrival=ARRIVAL)))
print('PASS: geometry, materials, matching collision, map references, unique entities, event compilation, recipe reconstruction.')
