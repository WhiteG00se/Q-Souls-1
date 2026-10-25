"""Static arena validation. Does not start or control Dark Souls Remastered."""
import argparse
from pathlib import Path, PureWindowsPath
import struct
import re
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game', default=r'D:/Games/Steam/steamapps/common/DARK SOULS REMASTERED')
parser.add_argument('--fixture', default=r'D:/AI/Temp/Q-Souls-1/hell/fixture/.local/respec')
parser.add_argument('--libraries', default=r'D:/AI/Tools/Python/qs1-map')
a = parser.parse_args()
sys.path.insert(0, a.libraries)
import soulstruct.logging_utils as _logging
_setup_log = _logging.setup
_logging.setup = lambda **kw: _setup_log(**(kw | {"log_path": Path(r'D:/AI/Temp/Q-Souls-1/hell/soulstruct-test.log')}))
import numpy as np
from soulstruct.darksouls1r.params import DrawParamBND
from soulstruct.base.maps.navmesh.nvm import NVM
from soulstruct.base.maps.navmesh.mcp import MCP
from soulstruct.base.maps.navmesh.mcg import MCG
from soulstruct.containers import TPF
from soulstruct.darksouls1r.maps import MSB
from soulstruct.darksouls1r.models import FLVER
from soulstruct.darksouls1r.events import EMEVD
from soulstruct.darksouls1r.ezstate import TalkESD
from soulstruct.base.ezstate.esd.ezl_parser import decompile
from soulstruct.base.ezstate.esd.esd_type import ESDType
from soulstruct.containers import Binder
from soulstruct.havok.core import HKX
from soulstruct.havok.fromsoft.shared.map_collision import MapCollisionModel
from soulstruct.darksouls1r.text import FMG
G, F = Path(a.game), Path(a.fixture)
CENTRE = np.array([1500., -380., 100.])

# Every pre-existing map entry retains all fields and references.
for name, additions in [('m10_02_00_00', 1), ('m14_01_00_00', 8)]:
    path = f'map/MapStudio/{name}.msb'
    before, after = MSB.from_path(G / path), MSB.from_path(F / path)
    assert len(after.get_parts()) == len(before.get_parts()) + additions
    for old_list, new_list in zip(before.get_all_subtype_lists(), after.get_all_subtype_lists()):
        assert len(new_list) >= len(old_list)
        for i, old in enumerate(old_list):
            assert old.to_json_dict(before, ignore_defaults=False) == new_list[i].to_json_dict(after, ignore_defaults=False), (name, old.name)
    if name.startswith('m14'):
        m = after
    else:
        return_start = next(p for p in after.player_starts if p.entity_id == 1020971)
        original_bonfire = next(o for o in after.objects if o.entity_id == 1021960)
        assert return_start.translate == next(s for s in before.spawn_points if s.entity_id == 1022960).spawn_point_region.translate
print('PASS: all original map records unchanged; only intended entries appended.')

# Validate what the renderer actually culls, not merely the triangle positions.
f = FLVER.from_path(F / 'map/m14_01_00_00/m9900B1A14.flver.dcx')
assert len(f.meshes) == 13
v = f.meshes[0].vertex_arrays[0].array['position']
faces = f.meshes[0].face_sets[0].vertex_indices
for mesh in f.meshes:
    a = mesh.vertex_arrays[0].array
    fs = mesh.face_sets[0].vertex_indices
    positions = a['position'][fs]
    geometric = np.cross(positions[:, 1] - positions[:, 0], positions[:, 2] - positions[:, 0])
    normal = a['normal'][fs].mean(1)
    assert np.all((geometric * normal).sum(1) < -1e-7), 'DS1 requires clockwise visible faces relative to their outward normals.'
    assert np.isfinite(a['position']).all() and np.isfinite(a['normal']).all()
    assert np.all(a['normal_w'] == 127), 'Preserve native NormalW packing.'
# Compare orientation independently with a native surface.
native = FLVER.from_path(G / 'map/m14_01_00_00/m0170B1A14.flver.dcx').meshes[0]
na = native.vertex_arrays[0].array
nf = native.face_sets[0].triangulate(True)
np_ = na['position'][nf]
assert np.all((np.cross(np_[:, 1]-np_[:, 0], np_[:, 2]-np_[:, 0]) * na['normal'][nf].mean(1)).sum(1) < 0)
r = np.linalg.norm(v[:, [0, 2]], axis=1)
assert np.max(np.abs(v[(r <= 24.001) & (v[:, 1] > -.1), 1])) < .001, '48 m fighting circle stays flat.'
assert 3.09 < v[:,1].max() < 3.11 and v[:,1].min() < -5.4
# The rim and ramp occupy real volume; no paper-thin outcrop meshes remain.
rim=(r>24.14)&(r<24.16)&(v[:,2]<-5)
assert rim.any() and np.allclose(v[rim,1],1.3)
lava_v=f.meshes[1].vertex_arrays[0].array['position']
assert np.allclose(lava_v[:,1],-3.2)
ordinary=lava_v[lava_v[:,2]<0]
assert 52.49 < np.linalg.norm(ordinary[:,[0,2]],axis=1).max() < 52.51
fall_a=f.meshes[2].vertex_arrays[0].array
assert np.isclose(fall_a['position'][:,1].min(),-28)
assert np.all(fall_a['color_0'][fall_a['position'][:,1]<-27.9]==0)
assert np.max(np.linalg.norm(fall_a['position'][fall_a['position'][:,2]<0][:,[0,2]],axis=1))<52.77
for mesh in f.meshes[3:]:
    a=mesh.vertex_arrays[0].array
    rock_v=np.unique(a['position'],axis=0)
    assert np.linalg.matrix_rank(rock_v-rock_v.mean(0))==3
    assert rock_v[:,1].min()<-3.2 and rock_v[:,1].max()>-2
    assert np.linalg.norm(rock_v[:,[0,2]],axis=1).min()>27.8
    # Weld the render vertices and prove every edge belongs to two faces.
    _,inverse=np.unique(a['position'],axis=0,return_inverse=True)
    fs=inverse[mesh.face_sets[0].vertex_indices]
    edges=np.sort(np.concatenate([fs[:,[0,1]],fs[:,[1,2]],fs[:,[2,0]]]),axis=1)
    assert np.all(np.unique(edges,axis=0,return_counts=True)[1]==2)
assert f.meshes[0].material == native.material
assert f.meshes[1].material == next(mesh.material for mesh in FLVER.from_path(G/'map/m14_01_00_00/m0010B1A14.flver.dcx').meshes if 'A14_lava_M_02' in mesh.material.mat_def_path)
for prefix in 'hl':
    path = f'map/m14_01_00_00/{prefix}14_01_00_00.hkxbhd'
    before, after = Binder.from_path(G/path), Binder.from_path(F/path)
    assert len(after.entries) == len(before.entries) + 1
    for old, new in zip(before.entries, after.entries):
        assert (old.entry_id, old.path, old.flags, old.data) == (new.entry_id, new.path, new.flags, new.data)
    c = MapCollisionModel.from_hkx(HKX.from_bytes(after.entries[-1].data))
    assert c.name == prefix+'9900B1' and c.meshes[0].material_index == 3
    cv, cf = c.meshes[0].vertices[:, :3], c.meshes[0].faces[:, :3]
    assert np.allclose(cv[cf], v[faces[:, [0,2,1]]]), 'Visual shell and collision must match after reversing rendering winding.'
    edge_counts = {}
    for face in cf:
        for x,y in [(face[0],face[1]),(face[1],face[2]),(face[2],face[0])]:
            edge=tuple(sorted((int(x),int(y))))
            edge_counts[edge]=edge_counts.get(edge,0)+1
    assert set(edge_counts.values())=={2}, 'Island must be a closed, solid manifold, not a 2D wall.'
    assert len(c.meshes)==2, 'Independent collision-only safety barrier required.'
    bv,bf=c.meshes[1].vertices[:,:3],c.meshes[1].faces[:,:3]
    edge_counts={}
    for face in bf:
        for x,y in [(face[0],face[1]),(face[1],face[2]),(face[2],face[0])]:
            key=tuple(sorted((int(x),int(y))));edge_counts[key]=edge_counts.get(key,0)+1
    assert set(edge_counts.values())=={2}, 'Safety barrier must be continuous and closed.'
    assert bv[:,1].max()>9.79 and bv[:,1].min()<=-1.
    # Both walls and their caps retain consistently oriented solid volume.
    for mesh in c.meshes:
        tri=mesh.vertices[mesh.faces[:,:3],:3]
        volume=np.einsum('ij,ij->i',tri[:,0],np.cross(tri[:,1],tri[:,2])).sum()/6
        assert volume>0, 'Collision must have outward-facing winding.'

# Sample the collision itself along the intended walking route, including
# both edges of the landing, rather than trusting the generator's metadata.
walk=cv[cf]
up=np.cross(walk[:,1]-walk[:,0],walk[:,2]-walk[:,0])[:,1]>1e-7
walk=walk[up]
def height_at(x,z):
    p=walk[:,:,[0,2]]
    a=p[:,1]-p[:,0];b=p[:,2]-p[:,0];q=np.array([x,z])-p[:,0]
    det=a[:,0]*b[:,1]-a[:,1]*b[:,0]
    u=(q[:,0]*b[:,1]-q[:,1]*b[:,0])/det
    t=(a[:,0]*q[:,1]-a[:,1]*q[:,0])/det
    hit=(u>=-1e-5)&(t>=-1e-5)&(u+t<=1.00001)
    assert hit.any(),(x,z)
    y=walk[:,0,1]+u*(walk[:,1,1]-walk[:,0,1])+t*(walk[:,2,1]-walk[:,0,1])
    return y[hit].min()
for x,z in [(0,0),(23,0),(-23,0),(0,-23),(0,23),(10,10)]:assert abs(height_at(x,z))<1e-4
for z in np.linspace(24.1,29.4,20):assert abs(height_at(0,z)-1.8*(z-24)/5.5)<.02
pad_z=29.5+np.sqrt(3.75**2-1.5**2)
for x,z in [(0,pad_z+.4),(0,pad_z-.8),(3.5,pad_z),(-3.5,pad_z),(0,pad_z+3.5)]:assert abs(height_at(x,z)-1.8)<.001
# The neck remains a rectangle; the enlarged landing has its own circular sides.
assert height_at(1.4,27.)>0
assert np.any((np.abs(v[:,0])>3.5)&(np.abs(v[:,2]-pad_z)<1.5)&(np.abs(v[:,1]-1.8)<.001))
print('PASS: native clockwise rendering, opaque floor/lava materials, solid rocky island, matching high/low collision, and reused outcrops.')

# The native sky is clipped at the horizon so there is darkness below the lava fall.
sky = FLVER.from_path(F/'map/m14_01_00_00/m9901B1A14.flver.dcx')
native_sky = FLVER.from_path(G/'map/m12_00_00_00/m9000B0A12.flver.dcx')
assert len(sky.meshes)==len(native_sky.meshes)==1
assert sky.meshes[0].material==native_sky.meshes[0].material
sky_v=sky.meshes[0].vertex_arrays[0].array['position']
assert sky_v[:,1].min()>=-3.201 and np.isclose(sky_v[:,1].max(),native_sky.meshes[0].vertex_arrays[0].array['position'][:,1].max()*.45)
old_pool=TPF.from_path(G/'map/m14/m14_9999.tpf.dcx'); pool=TPF.from_path(F/'map/m14/m14_9999.tpf.dcx')
assert len(pool.textures)==len(old_pool.textures)+1
for old,new in zip(old_pool.textures,pool.textures):assert old==new
source_sky=next(t for t in TPF.from_path(G/'map/m12/m12_9999.tpf.dcx').textures if t.stem=='m12_sky')
assert pool.textures[-1]==source_sky
old_light=DrawParamBND.from_path(G/'param/DrawParam/a14_DrawParam.parambnd.dcx')
light=DrawParamBND.from_path(F/'param/DrawParam/a14_DrawParam.parambnd.dcx')
for bank,old in old_light.draw_params_0.items():
    new=light.draw_params_0[bank]
    assert set(new.rows)==set(old.rows)|{110}, (bank, set(new.rows)-set(old.rows))
    for rid,row in old.rows.items():assert row==new.rows[rid],(bank,rid)
assert light.draw_params_0['LightBank'].rows[110].TopDownLightAlpha>=100
assert light.draw_params_0['LightBank'].rows[110].SpecularLightAlpha==200
assert light.draw_params_0['FogBank'].rows[110].FogStartDistance>=140
print('PASS: reused outdoor night sky and original sky texture; dedicated readable outdoor lighting; original lighting and textures unchanged.')

bonfire = next(o for o in m.objects if o.entity_id == 1411965)
actor = next(c for c in m.characters if c.entity_id == 1410965)
spawn = next(s for s in m.spawn_points if s.entity_id == 1412965)
arrival = next(p for p in m.player_starts if p.entity_id == 1410971)
col = next(c for c in m.collisions if c.name == 'h9900B1')
part = next(p for p in m.map_pieces if p.name == 'm9900B1')
assert np.allclose(bonfire.translate, CENTRE+[0.,1.8,pad_z+.4])
assert np.allclose(arrival.translate,CENTRE+[0.,1.85,pad_z-.8])
assert bonfire.rotate.y==180 and arrival.rotate.y==0
assert col.place_name_banner_id+10000==19990 and col.force_place_name_banner
assert col.environment_event.entity_id==8 and col.environment_event.unk_x00_x04==110
# Verify the game's actual GI resource format uses the environment entity ID,
# independently of the bake-lighting row (first subtype field).
for area in [10,14]:
    rel=f'map/m{area}/GI_EnvM_m{area}.tpfbhd'
    if area==14:
        old_probe=Binder.from_path(G/rel);new_probe=Binder.from_path(F/rel)
        assert len(new_probe.entries)==len(old_probe.entries)+1
        for old,new in zip(old_probe.entries,new_probe.entries):assert (old.path,old.entry_id,old.data)==(new.path,new.entry_id,new.data)
        entry=new_probe.entries[-1]
        assert entry.name==f'GI_Env_m14_01_{col.environment_event.entity_id:04d}.tpf.dcx'
        updated_probe=TPF.from_bytes(entry.data)
    else:
        source_probe=TPF.from_bytes(next(e.data for e in Binder.from_path(G/rel).entries if e.name=='GI_Env_m10_02_0000.tpf.dcx'))
for old,new in zip(source_probe.textures,updated_probe.textures):
    assert old.data==new.data and old.texture_type==new.texture_type and old.mipmap_count==new.mipmap_count
    assert new.stem==old.stem.replace('m10_02_0000','m14_01_0008')
assert col.attached_bonfire == bonfire.entity_id
# Validate the real Steam executable's gate, not only mod-internal ID consistency.
# GetBonfireKey adds 1000 to the talk actor ID and accepts suffixes 1950..1969.
engine=(G/'DarkSoulsRemastered.exe').read_bytes()
gate=bytes.fromhex('8d8862f8ffff83f913410f46d9')
assert engine.count(gate)==1, 'Native bonfire ID gate changed; re-inspect the supported game version.'
lower=-struct.unpack_from('<i',gate,2)[0]
upper=lower+gate[8]
assert lower <= bonfire.entity_id % 10000 <= upper
assert not lower <= 1411970 % 10000 <= upper, 'Regression: the original Hell ID must be rejected by the native gate.'

assert bonfire.entity_id == actor.entity_id + 1000 == spawn.entity_id - 1000
assert actor.talk_id == 141020 and actor.character_id == 100000
native_map=MSB.from_path(G/'map/MapStudio/m14_01_00_00.msb')
for entry,seq,prefix in [(actor,native_map.characters,'c1000'),(bonfire,native_map.objects,'o0200')]:
    ids=[int(e.name[-4:]) for e in seq if re.fullmatch(prefix+r'_\d{4}',e.name)]
    assert entry.name==f'{prefix}_{max(ids)+1:04d}'
assert col.ambient_light_id == part.ambient_light_id == actor.ambient_light_id == 110
assert bonfire.draw_parent is col and actor.draw_parent is col
# The engine resolves actual asset filenames from model SIB paths. Empty paths
# serialize successfully but leave the player in a void with no floor.
assert PureWindowsPath(col.model.sib_path).stem == col.model.name == 'h9900B1'
assert PureWindowsPath(part.model.sib_path).stem == part.model.name == 'm9900B1'
assert PureWindowsPath(col.model.sib_path).suffix.lower() == '.hkxwin'
assert PureWindowsPath(part.model.sib_path).suffix.lower() == '.sib'
assert len(next(e for e in Binder.from_path(F/'map/m14_01_00_00/h14_01_00_00.hkxbhd').entries if e.name == PureWindowsPath(col.model.sib_path).stem + 'A14.hkx.dcx').data) > 0
assert (F/'map/m14_01_00_00'/(PureWindowsPath(part.model.sib_path).stem+'A14.flver.dcx')).is_file()
assert col.display_groups.enabled_bits == part.draw_groups.enabled_bits
assert not any(c.display_groups.enabled_bits & col.display_groups.enabled_bits for c in m.collisions if c is not col)
assert np.allclose(arrival.translate, spawn.spawn_point_region.translate)
assert np.linalg.norm(np.array(list(arrival.translate)) - np.array(list(bonfire.translate))) < 1.3
assert len(m.characters) == len(MSB.from_path(G / 'map/MapStudio/m14_01_00_00.msb').characters) + 1
assert not any(p.name.startswith('QS1') for p in m.connect_collisions)
print('PASS: engine model lookup paths resolve to generated terrain and collision assets.')
print('PASS: isolated draw group; bonfire actor, attached collision, respawn and safe arrival references; no enemies added.')

# Two connected rooms keep the native bonfire actor in normal backread.
old_m=MSB.from_path(G/'map/MapStudio/m14_01_00_00.msb')
old_rooms=MCP.from_path(G/'map/m14_01_00_00/m14_01_00_00.mcp')
rooms=MCP.from_path(F/'map/m14_01_00_00/m14_01_00_00.mcp')
assert len(rooms.aabbs)==len(m.navmeshes)==len(old_m.navmeshes)+2
for old,new in zip(old_rooms.aabbs,rooms.aabbs):assert old==new
base=len(old_rooms.aabbs)
assert rooms.aabbs[-2].connected_navmesh_part_indices==[base+1]
assert rooms.aabbs[-1].connected_navmesh_part_indices==[base]
for room,nav in zip(rooms.aabbs[-2:],list(m.navmeshes)[-2:]):
    assert nav.navmesh_groups.enabled_bits==col.navmesh_groups.enabled_bits
    assert np.all(np.array(list(room.aabb_start)) <= CENTRE+[0,.5,0])
    assert np.all(np.array(list(room.aabb_end)) >= CENTRE+[0,.5,0])
assert np.all(np.array(list(rooms.aabbs[-2].aabb_end)) >= np.array(list(bonfire.translate)))
old_graph=MCG.from_path(G/'map/m14_01_00_00/m14_01_00_00.mcg')
graph=MCG.from_path(F/'map/m14_01_00_00/m14_01_00_00.mcg')
assert len(graph.nodes)==len(old_graph.nodes)+2 and len(graph.edges)==len(old_graph.edges)+2
idx=lambda seq,obj:next(i for i,x in enumerate(seq) if x is obj)
for old,new in zip(old_graph.nodes,graph.nodes):
    assert old.translate==new.translate and old.dead_end_navmesh_index==new.dead_end_navmesh_index
    assert old.unknown_offset==new.unknown_offset
    assert [idx(old_graph.nodes,x) for x in old.connected_nodes]==[idx(graph.nodes,x) for x in new.connected_nodes]
    assert [idx(old_graph.edges,x) for x in old.connected_edges]==[idx(graph.edges,x) for x in new.connected_edges]
for old,new in zip(old_graph.edges,graph.edges):
    assert (old.navmesh_index,old.map_id,old.node_a_triangles,old.node_b_triangles,old.cost)==(new.navmesh_index,new.map_id,new.node_a_triangles,new.node_b_triangles,new.cost)
assert {e.navmesh_index for e in graph.edges[-2:]}=={base,base+1}
assert all(idx(graph.nodes,e.node_a)>=len(old_graph.nodes) and idx(graph.nodes,e.node_b)>=len(old_graph.nodes) for e in graph.edges[-2:])
old_n=Binder.from_path(G/'map/m14_01_00_00/m14_01_00_00.nvmbnd.dcx')
new_n=Binder.from_path(F/'map/m14_01_00_00/m14_01_00_00.nvmbnd.dcx')
assert len(new_n.entries)==len(old_n.entries)+2
for old,new in zip(old_n.entries,new_n.entries):assert old.data==new.data and old.path==new.path
for nav,e in zip(list(m.navmeshes)[-2:],new_n.entries[-2:]):
    assert PureWindowsPath(nav.model.sib_path).stem == nav.model.name
    assert e.name == nav.model.name + 'A14.nvm'
    assert e.name.replace(nav.model.name, old_m.navmesh_models[0].name) == old_n.entries[0].name
    n=NVM.from_bytes(e.get_uncompressed_data())
    assert len(n.triangles)>20
    tri = np.array([t.vertex_indices for t in n.triangles])
    points = n.vertices[tri]
    assert np.all(np.cross(points[:,1]-points[:,0], points[:,2]-points[:,0])[:,1] > 0)
    assert all(all(j==-1 or 0<=j<len(n.triangles) for j in t.connected_indices) for t in n.triangles)
print('PASS: complete native navigation/loading graph; bonfire covered by active rooms; no navigation connection to vanilla routes; original graph entries preserved.')

# Existing events retain all instructions, argument replacements, and restart behavior.
for name, event_id, request, target in [('m10_02_00_00',11025951,11025950,1410971),('m14_01_00_00',11415951,11415950,1020971)]:
    path = f'event/{name}.emevd.dcx'
    before, after = EMEVD.from_path(G/path), EMEVD.from_path(F/path)
    assert set(after.events) == set(before.events) | {event_id}
    for eid, old in before.events.items():
        new = after.events[eid]
        assert old.on_rest_behavior == new.on_rest_behavior
        expected = [i.to_numeric() for i in old.instructions]
        actual = [i.to_numeric() for i in new.instructions]
        assert actual[:len(expected)] == expected, (name,eid)
        if eid != 0:
            assert actual == expected
    # Bonfire rest can arrive after menu selection. It must not restart the
    # travel event after consuming its request and cancel the pending warp.
    assert int(after.events[event_id].on_rest_behavior)==0, 'Travel must continue through bonfire rest.'
    instructions = after.events[event_id].instructions
    assert any(i.category == 2003 and i.index == 14 and i.args_list[-1] == target for i in instructions)
    assert any(i.category == 2003 and i.index == 2 and request in i.args_list for i in after.events[0].instructions[len(before.events[0].instructions):])
print('PASS: original event instructions/arguments unchanged; bidirectional warp targets and load-time request clearing.')

# Verify menu changes against the original scripts after applying only the established respec edit.
def num(data):
    return data[0]-64 if len(data)==2 else struct.unpack_from('<i',data,1)[0]
def command(c):
    return (c.bank,c.index,tuple(c.args))
def condition(c):
    return (c.next_state_id,c.test_ezl,tuple(command(x) for x in c.pass_commands),tuple(condition(x) for x in c.subconditions))
extra_rows=[]
for path in sorted((F/'script/talk').glob('*.talkesdbnd.dcx')):
    original=Binder.from_path(G/'script/talk'/path.name)
    updated=Binder.from_path(path)
    expected_extra = 1 if path.name.startswith('m14_01') else 0
    assert len(updated.entries)==len(original.entries)+expected_extra
    for entry in updated.entries:
        esd=TalkESD.from_bytes(entry.data)
        for states in esd.state_machines.values():
            for state in states.values():
                for c in state.enter_commands:
                    if c.bank==1 and c.index==19 and num(c.args[1]) in [15000991,15000992]:
                        extra_rows.append((path.name,entry.name,num(c.args[1]),num(c.args[0]),num(c.args[2])))
        if entry.name=='t141020.esd':
            baseline=TalkESD.from_bytes(next(e.data for e in original.entries if e.name=='t141000.esd'))
        else:
            baseline=TalkESD.from_bytes(next(e.data for e in original.entries if e.name==entry.name))
        is_travel=entry.name in ['t100200.esd','t141020.esd']
        for gid, old_states in baseline.state_machines.items():
            new_states=esd.state_machines[gid]
            assert len(new_states)==len(old_states)+(1 if is_travel else 0)
            for sid,old in old_states.items():
                new=new_states[sid]
                assert [command(c) for c in old.exit_commands]==[command(c) for c in new.exit_commands]
                assert [command(c) for c in old.ongoing_commands]==[command(c) for c in new.ongoing_commands]
                expected=[]
                for c in old.enter_commands:
                    bank,idx,arguments=command(c)
                    if bank==1 and idx==19 and num(arguments[1])==15000100:
                        arguments=(arguments[0],b'\x82'+struct.pack('<i',15000990)+b'\xa1',b'\x3f\xa1')
                    expected.append((bank,idx,arguments))
                actual=[command(c) for c in new.enter_commands]
                assert actual[:len(expected)]==expected,(path.name,entry.name,sid)
                transitions=[condition(c) for c in old.conditions]
                assert [condition(c) for c in new.conditions][:len(transitions)]==transitions
                if len(actual)>len(expected):
                    assert is_travel and len(actual)==len(expected)+1
                    assert len(new.conditions)==len(old.conditions)+1
                    travel_condition=new.conditions[-1]
                    assert decompile(travel_condition.test_ezl, ESDType.TALK) == 'GetTalkListEntryResult() == 13'
                    travel=new_states[travel_condition.next_state_id]
                    assert any(c.index==11 and num(c.args[0])==(11025950 if entry.name=='t100200.esd' else 11415950) for c in travel.enter_commands)
                    assert any(c.index==43 for c in travel.enter_commands)
assert sorted(extra_rows)==sorted([('m10_02_00_00.talkesdbnd.dcx','t100200.esd',15000991,13,-1),('m14_01_00_00.talkesdbnd.dcx','t141020.esd',15000992,13,-1)])
text_tables = 0
for path in (F/'msg').glob('*/menu.msgbnd.dcx'):
    b=Binder.from_path(path)
    for e in b.entries:
        if e.name=='Event_text_.fmg':
            text_tables += 1
            texts=FMG.from_bytes(e.data)
            assert texts.entries[15000991]=='Travel to Cursed Arena'
            assert texts.entries[15000992]=='Travel to Firelink Shrine'
assert text_tables >= 13
place_tables=0
for path in (F/'msg').glob('*/item.msgbnd.dcx'):
    before=Binder.from_path(G/'msg'/path.parent.name/path.name)
    after=Binder.from_path(path)
    for old,new in zip(before.entries,after.entries):
        if old.name=='Place_name_.fmg':
            source=FMG.from_bytes(old.data);updated=FMG.from_bytes(new.data)
            assert 9990 not in source.entries and 19990 not in source.entries
            assert updated.entries[9990]=='Cursed Arena' and updated.entries[19990]==' '
            assert updated.entries==source.entries|{9990:'Cursed Arena',19990:' '}
            assert source.entries[1020] and source.entries[11020]==' ', 'Match the native Firelink title/subtitle convention.'
            place_tables+=1
        else:assert old.data==new.data
assert place_tables>=13
print('PASS: exactly two extra menu rows, available without Lordvessel; original menu commands/transitions preserved; all languages contain exact travel labels.')
# Reserve a full native bonfire kindle block plus transient requests/event IDs.
reserved = set(range(11410944, 11410952)) | {11025950, 11025951, 11415950, 11415951}
for path in (G/'event').glob('*.emevd.dcx'):
    original = EMEVD.from_path(path)
    assert not reserved.intersection(original.events), path.name
    for event in original.events.values():
        for instruction in event.instructions:
            assert not reserved.intersection(instruction.args_list), (path.name, event.event_id)
print('PASS: new event/request IDs and the native bonfire flag block do not collide with vanilla events.')

# Native area headlines are named controls, not Place_name FMG entries. The
# engine selects mapname_AA_BB_C by splitting the collision ID into AA/BB/C.
def layout(path):
    import zlib
    raw=path.read_bytes()
    data=zlib.decompress(raw[76:]) if raw[:4]==b'DCX\0' else raw
    blocks={};position=16
    while position<len(data):
        name=data[position:position+4].rstrip(b'\0').decode()
        size,count=struct.unpack_from('<ii',data,position+4)
        blocks[name]=(data[position+16:position+16+size],count)
        position+=16+size
    return blocks

def integer(b,p):return struct.unpack_from('<i',b,p)[0]
def ui_string(b,p):
    end=p
    while b[end:end+2]!=b'\0\0':end+=2
    return b[p:end].decode('utf-16-le')

def headline_name(place):return f'mapname_{place//100%100:02d}_{place//10%10:02d}_{place%10}'
assert headline_name(col.place_name_banner_id)=='mapname_99_09_0'
for relative in ['menu/menu.drb','menu/menu.drb.dcx']:
    old,new=layout(G/relative),layout(F/relative)
    assert set(old)==set(new)
    assert old['DLG'][1]==new['DLG'][1]
    for section in old:
        if section not in ['STR','SHAP','SHPR','CTRL','DLGO','DLG']:assert old[section]==new[section]
    private=[]
    for g in range(old['DLG'][1]):
        a=old['DLG'][0][g*64:(g+1)*64];b=new['DLG'][0][g*64:(g+1)*64]
        name=ui_string(old['STR'][0],integer(a,0))
        assert ui_string(new['STR'][0],integer(b,0))==name
        assert a[:32]==b[:32] and a[40:]==b[40:]
        count=integer(a,32)
        assert integer(b,32)==count+(2 if name=='C01-03' else 1 if name=='F23_MapName' else 0)
        for j in range(count):
            x=integer(a,36)+j*32;y=integer(b,36)+j*32
            assert old['DLGO'][0][x:x+32]==new['DLGO'][0][y:y+32],(name,j)
        if name=='F23_MapName':
            offset=integer(b,36)+count*32
            item=new['DLGO'][0][offset:offset+32]
            assert ui_string(new['STR'][0],integer(item,0))==headline_name(col.place_name_banner_id)
            record=new['SHAP'][0][integer(item,4):integer(item,4)+8]
            assert ui_string(new['STR'][0],integer(record,0))=='Text'
            start=integer(record,4);data=new['SHPR'][0][start:]
            base=16 if relative.endswith('.dcx') else 8
            assert struct.unpack_from('<4h',data)==(256,0,1024,56)
            assert struct.unpack_from('<h',data,base+12)[0]==10
            assert data[base+14:base+16]==bytes([10,0])
            assert ui_string(new['STR'][0],integer(data,base+20))=='Cursed Arena'
            private.append(item)
    assert len(private)==1
print('PASS: dedicated native headline control resolves the private collision ID, configures centered large text above the line, and preserves every original banner control in both layout formats.')

print('PASS: all arena static checks; game never launched.')
