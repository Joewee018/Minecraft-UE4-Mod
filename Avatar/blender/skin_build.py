"""CRB-AVATAR-01 step A1 (Blender 3.6, headless):
  blender -b --factory-startup -P skin_build.py -- <clip.fbx> <out_dir> [apose_deg]

Builds ONE continuous low-poly body in the style of Avatar/reference_style.jpg (box-modelled base mesh, A-pose,
mitten hands with a thumb, simple shoes, faceted shading) with Blender's Skin modifier around the Mixamo skeleton
imported from <clip.fbx>; sets the armature rest pose to the same A-pose; binds with automatic (bone heat) weights;
Smart-UV-unwraps (head islands get 3x texel density) and writes:
  out/skeleton.json   rest pose of every bone (A-pose), for reference
  out/body_mesh.json  verts, polys, loop UVs, per-vertex weights (consumed by Avatar/gen/paint_avatar.py)
  out/avatar_rig.blend  the rig + mesh, reopened by build_fbx.py after the texture is painted
  out/preview_*.png   workbench renders (front, 3/4, side, back) to compare with the style reference
Original geometry only; no third-party meshes."""
import bpy, bmesh, json, math, os, sys
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index('--') + 1:]
clip, out = argv[0], argv[1]
apose = math.radians(float(argv[2]) if len(argv) > 2 else 40.0)
os.makedirs(out, exist_ok=True)
P = 'mixamorig:'

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=clip, automatic_bone_orientation=False, ignore_leaf_bones=False)
arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
for o in list(bpy.context.scene.objects):          # keep only the skeleton (drop any bundled character mesh)
    if o.type != 'ARMATURE': bpy.data.objects.remove(o, do_unlink=True)
if arm.animation_data: arm.animation_data.action = None
arm.name = 'Armature'; arm.data.name = 'Armature'
# Bake the FBX import scale into the armature so the bones are in world metres.
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

def J(name):
    b = arm.data.bones.get(P + name)
    return (arm.matrix_world @ b.head_local) if b else None

up = (J('HeadTop_End') - J('Hips')).normalized()
left = (J('LeftArm') - J('RightArm')); left = (left - up * left.dot(up)).normalized()
fwd = left.cross(up).normalized()
height = (J('HeadTop_End') - J('Hips')).dot(up) + (J('Hips') - J('LeftToe_End')).dot(up) + 0.02
S = height / 1.80

# ---------------- A-pose: rotate each arm chain about its shoulder joint (rest pose of the rig AND the mesh)
bpy.ops.object.mode_set(mode='EDIT')
eb = arm.data.edit_bones
for side, sg in (('Left', 1), ('Right', -1)):
    pivot = eb[P + side + 'Arm'].head.copy()
    rot = Matrix.Rotation(-sg * apose, 4, fwd)        # arm goes down
    M = Matrix.Translation(pivot) @ rot @ Matrix.Translation(-pivot)
    root = eb[P + side + 'Arm']
    for b in [root] + list(root.children_recursive):
        b.transform(M, scale=False, roll=True)
bpy.ops.object.mode_set(mode='OBJECT')
J = (lambda name: (lambda b: (arm.matrix_world @ b.head_local) if b else None)(arm.data.bones.get(P + name)))

skel = [{'name': b.name, 'parent': b.parent.name if b.parent else None, 'head': list(arm.matrix_world @ b.head_local),
         'tail': list(arm.matrix_world @ b.tail_local)} for b in arm.data.bones]
import zlib
_strip = lambda n: n[len(P):] if n and n.startswith(P) else (n or '')
_lines = sorted('%s>%s' % (_strip(b['name']), _strip(b['parent'])) for b in skel)
skel_crc = zlib.crc32(''.join(l + '\n' for l in _lines).encode('utf-8')) & 0xffffffff
print('SKELETON bones=%d crc32=%08X' % (len(skel), skel_crc))
json.dump({'bones': skel, 'bone_count': len(skel), 'skeleton_crc32': skel_crc, 'height': height, 'up': list(up), 'left': list(left), 'fwd': list(fwd), 'apose_deg': math.degrees(apose)},
          open(os.path.join(out, 'skeleton.json'), 'w'), indent=1)

# ---------------- skin skeleton (vertex graph + radii), proportions like the style reference
nodes, edges, radii = [], [], []
def node(p, rx, ry=None):
    nodes.append(Vector(p)); radii.append((rx * S, (ry if ry is not None else rx) * S)); return len(nodes) - 1
def chain(*ids):
    for a, b in zip(ids, ids[1:]): edges.append((a, b))

hips, spine, spine1, spine2, neck, head, top = (J(n) for n in ('Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'HeadTop_End'))
n_pelvis = node(hips - up * 0.04 * S, 0.150, 0.100)
n_waist = node(spine + up * 0.02 * S, 0.135, 0.095)
n_chest = node(spine1 + up * 0.05 * S, 0.160, 0.110)
n_upper = node(spine2 + up * 0.05 * S, 0.175, 0.105)
n_neck = node(neck + up * 0.03 * S, 0.055, 0.058)
n_jaw = node(head + up * 0.04 * S + fwd * 0.012 * S, 0.072, 0.085)
n_skull = node(head + up * 0.12 * S, 0.088, 0.100)
n_top = node(top - up * 0.035 * S, 0.060, 0.070)
chain(n_pelvis, n_waist, n_chest, n_upper, n_neck, n_jaw, n_skull, n_top)
for side, sg in (('Left', 1), ('Right', -1)):
    sh = J(side + 'Arm'); el = J(side + 'ForeArm'); wr = J(side + 'Hand'); mid = J(side + 'HandMiddle1'); th = J(side + 'HandThumb2')
    n_sh = node(sh - (sh - spine2).normalized() * 0.02 * S, 0.068, 0.070)
    n_el = node(el, 0.042, 0.044)
    n_wr = node(wr, 0.030, 0.026)
    n_hand = node(wr + (mid - wr) * 1.35, 0.034, 0.016)       # mitten
    n_th = node(th if th is not None else wr + fwd * 0.04 * S, 0.012, 0.012)
    chain(n_upper, n_sh, n_el, n_wr, n_hand); edges.append((n_wr, n_th))
    ul = J(side + 'UpLeg'); kn = J(side + 'Leg'); an = J(side + 'Foot'); toe = J(side + 'ToeBase'); toe_end = J(side + 'Toe_End')
    n_hip = node(ul, 0.085, 0.090)
    n_kn = node(kn, 0.055, 0.058)
    n_calf = node(kn + (an - kn) * 0.35, 0.058, 0.064)
    n_an = node(an, 0.040, 0.044)
    ground = min(toe.dot(up), toe_end.dot(up)) - 0.02 * S
    n_ball = node(toe - up * (toe.dot(up) - ground - 0.035 * S), 0.048, 0.035)
    n_tip = node(toe_end - up * (toe_end.dot(up) - ground - 0.03 * S), 0.035, 0.028)
    chain(n_pelvis, n_hip, n_kn, n_calf, n_an, n_ball, n_tip)

me = bpy.data.meshes.new('AvatarBody')
me.from_pydata([tuple(p) for p in nodes], edges, [])
body = bpy.data.objects.new('AvatarBody', me)
bpy.context.scene.collection.objects.link(body)
sk = body.modifiers.new('Skin', 'SKIN')
sk.branch_smoothing = 0.6
sk.use_smooth_shade = False
for i, r in enumerate(radii):
    sv = me.skin_vertices[0].data[i]
    sv.radius = r
    sv.use_root = (i == n_pelvis)
sub = body.modifiers.new('Subdivide', 'SUBSURF'); sub.levels = 1; sub.render_levels = 1
for name in ('Skin', 'Subdivide'):
    with bpy.context.temp_override(object=body, active_object=body):
        bpy.ops.object.modifier_apply(modifier=name)
# face plane: flatten the front of the face a little (the reference's helmet-like head with a face plane)
bm = bmesh.new(); bm.from_mesh(me)
hc = (J('Head') + up * 0.10 * S)
for v in bm.verts:
    d = v.co - hc
    if d.dot(up) > -0.13 * S and d.dot(up) < 0.06 * S and d.dot(fwd) > 0.05 * S and abs(d.dot(left)) < 0.09 * S:
        v.co -= fwd * (d.dot(fwd) - 0.05 * S) * 0.5
bm.to_mesh(me); bm.free()
for p in me.polygons: p.use_smooth = False

# ---------------- bind: automatic (bone heat) weights; fall back to envelopes if heat weighting fails
body.select_set(True); arm.select_set(True)
bpy.context.view_layer.objects.active = arm
with bpy.context.temp_override(selected_editable_objects=[body, arm], selected_objects=[body, arm], active_object=arm, object=arm):
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
if not any(len(v.groups) for v in me.vertices):
    print('WARN bone heat failed; using envelope weights')
    with bpy.context.temp_override(selected_editable_objects=[body, arm], selected_objects=[body, arm], active_object=arm, object=arm):
        bpy.ops.object.parent_set(type='ARMATURE_ENVELOPE')
# keep at most 4 influences per vertex (engine limit) and normalise
for v in me.vertices:
    gs = sorted(v.groups, key=lambda g: -g.weight)
    for g in gs[4:]: body.vertex_groups[g.group].remove([v.index])
    tot = sum(g.weight for g in gs[:4]) or 1.0
    for g in gs[:4]: body.vertex_groups[g.group].add([v.index], g.weight / tot, 'REPLACE')

# ---------------- UVs: smart project, head islands at 3x density, pack
bpy.context.view_layer.objects.active = body
for o in bpy.context.scene.objects: o.select_set(o == body)
with bpy.context.temp_override(object=body, active_object=body, selected_objects=[body], selected_editable_objects=[body]):
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004, area_weight=0.0)
    bpy.ops.object.mode_set(mode='OBJECT')
head_gid = body.vertex_groups[P + 'Head'].index if (P + 'Head') in body.vertex_groups else -1
uvl = me.uv_layers.active.data
def is_head(poly):
    return all(any(g.group == head_gid and g.weight > 0.5 for g in me.vertices[vi].groups) for vi in poly.vertices)
for poly in me.polygons:
    if is_head(poly):
        c = sum((uvl[li].uv for li in poly.loop_indices), Vector((0, 0))) / poly.loop_total
        for li in poly.loop_indices: uvl[li].uv = c + (uvl[li].uv - c) * 3.0
with bpy.context.temp_override(object=body, active_object=body, selected_objects=[body], selected_editable_objects=[body]):
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.select_all(action='SELECT')
    bpy.ops.uv.pack_islands(rotate=False, margin=0.006)
    bpy.ops.object.mode_set(mode='OBJECT')

# ---------------- export the mesh data for the photo painter
names = [g.name for g in body.vertex_groups]
data = {'verts': [list(body.matrix_world @ v.co) for v in me.vertices],
        'polys': [list(p.vertices) for p in me.polygons],
        'loop_uvs': [[list(uvl[li].uv) for li in p.loop_indices] for p in me.polygons],
        'weights': [{names[g.group]: round(g.weight, 4) for g in v.groups} for v in me.vertices],
        'up': list(up), 'left': list(left), 'fwd': list(fwd), 'height': height,
        'joints': {b['name']: b['head'] for b in skel},
        'stats': {'verts': len(me.vertices), 'polys': len(me.polygons), 'tris': sum(len(p.vertices) - 2 for p in me.polygons)}}
json.dump(data, open(os.path.join(out, 'body_mesh.json'), 'w'))
print('MESH', json.dumps(data['stats']))
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, 'avatar_rig.blend'))

# ---------------- workbench previews (grey, flat) to compare with the style reference
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'; scene.display.shading.color_type = 'SINGLE'
scene.display.shading.single_color = (0.8, 0.8, 0.8)
scene.render.resolution_x, scene.render.resolution_y = 700, 1000
scene.world = bpy.data.worlds.new('w'); scene.world.color = (0.05, 0.05, 0.05)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = 'ORTHO'; cam.data.ortho_scale = height * 1.1
ctr = hips + up * (height * 0.5 - hips.dot(up) + (J('LeftToe_End').dot(up)))
for name, d in (('front', fwd), ('three_quarter', (fwd + left).normalized()), ('side', left), ('back', -fwd)):
    cam.location = ctr + d * 6.0
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler() if up.z > 0.9 else (-d).to_track_quat('-Z', 'Z').to_euler()
    scene.render.filepath = os.path.join(out, 'preview_%s.png' % name)
    bpy.ops.render.render(write_still=True)
print('DONE')
