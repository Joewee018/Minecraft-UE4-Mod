"""Physics & Portal mod - rig and animate the user-supplied sculpted 3D Steve (Blender 3.6, headless). PRIVATE BUILD.
The model is used exactly as supplied (its own geometry, proportions and its own steve.png texture); the only change
is the rest pose made by Avatar/steve3d/unpose_steve3d.py (standing, arms down) so it can be skinned.
  blender -b --factory-startup -P steve3d_build.py -- <steve3d_rest.npz> <skin.png> <out_dir>

Outputs in <out_dir>: fbx/Steve3D.fbx (SK_PPSteve), fbx/P_<Clip>.fbx (physics-mod clips), T_Steve3D_Skin.png,
skeleton.json (crc), clips.json, preview_*.png.
Frame: Z up, the character faces -Y, character's right = -X, metres. Unreal-style IK helper bones (ik_foot_root,
ik_foot_l/r, ik_hand_root, ik_hand_l/r) follow the hands and feet in every clip for IK-driven adjustments."""
import bpy, json, math, os, sys, zlib, shutil
import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
npz_path, skin_path, out = argv[0], argv[1], argv[2]
fbx_dir = os.path.join(out, 'fbx'); os.makedirs(fbx_dir, exist_ok=True)
FPS = 30
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS

d = np.load(npz_path)
verts, faces, uvs, weights = d['verts'], d['faces'], d['uvs'], d['weights']
names = [str(n) for n in d['names']]
joints = {str(n): Vector(map(float, p)) for n, p in zip(d['joint_names'], d['joints'])}

# drop vertices no face uses (the prop block that came with the pose)
used = np.zeros(len(verts), bool); used[faces.ravel()] = True
remap = -np.ones(len(verts), np.int64); remap[used] = np.arange(used.sum())
verts, weights, faces = verts[used], weights[used], remap[faces]
# rigid block legs: the shin weights fold into the thigh so nothing can crease at a knee
for side in ('r', 'l'):
    li, si = names.index('leg_' + side), names.index('shin_' + side)
    weights[:, li] += weights[:, si]; weights[:, si] = 0
# Each sculpted piece (shoe, trouser leg, cuff) belongs wholly to one leg. The UV split labels a piece's inner half by
# x against the body centre, which tears a shoe or trouser leg that crosses it and smears it across both legs once
# they spread. Re-assign every leg piece to the leg most of its vertices already belong to.
def pieces(nv, f):
    lab = np.arange(nv)
    while True:
        m = lab[f].min(1)
        new = lab.copy(); np.minimum.at(new, f.ravel(), np.repeat(m, 3))
        new = new[new]
        if np.array_equal(new, lab): return lab
        lab = new
lab = pieces(len(verts), faces)
LR, LL = names.index('leg_r'), names.index('leg_l')
legw = weights[:, LR] + weights[:, LL]
fixed = 0
for c in np.unique(lab[legw > 0.01]):
    m = lab == c
    r, l = weights[m, LR].sum(), weights[m, LL].sum()
    if max(r, l) < 0.6 * (r + l): continue          # both legs in one piece: leave it
    keep, drop = (LR, LL) if r >= l else (LL, LR)
    fixed += int((weights[m, drop] > 0).sum())
    weights[m, keep] += weights[m, drop]; weights[m, drop] = 0
print('STEVE3D legs: %d leg pieces, %d vertices moved to their piece\'s leg' % (len(np.unique(lab[legw > 0.01])), fixed))

shutil.copyfile(skin_path, os.path.join(out, 'T_Steve3D_Skin.png'))
skin = bpy.data.images.load(os.path.join(out, 'T_Steve3D_Skin.png')); skin.name = 'T_Steve3D_Skin'

# ------------------------------------------------------------------ armature (every bone points up, roll 0)
# local X = +X (character's left), local Y = up, local Z = forward (-Y).  ZYX euler (Z applied first):
#   rot X > 0 tips the up end forward (a hanging limb swings BACK; negative swings it forward)
#   rot Z > 0 swings a hanging limb toward +X (outward for left limbs, inward for right limbs)
#   rot Y > 0 turns toward the character's left
J = joints
BONES = [('root', None, Vector((0, 0, 0))), ('pelvis', 'root', J['pelvis']), ('body', 'pelvis', J['body']), ('head', 'body', J['head']),
         ('arm_r', 'body', J['arm_r']), ('forearm_r', 'arm_r', J['forearm_r']), ('arm_l', 'body', J['arm_l']), ('forearm_l', 'arm_l', J['forearm_l']),
         ('leg_r', 'pelvis', J['leg_r']), ('shin_r', 'leg_r', J['shin_r']), ('leg_l', 'pelvis', J['leg_l']), ('shin_l', 'leg_l', J['shin_l']),
         ('ik_foot_root', 'root', Vector((0, 0, 0))), ('ik_foot_r', 'ik_foot_root', J['leg_end_r']), ('ik_foot_l', 'ik_foot_root', J['leg_end_l']),
         ('ik_hand_root', 'root', Vector((0, 0, 0))), ('ik_hand_r', 'ik_hand_root', J['arm_end_r']), ('ik_hand_l', 'ik_hand_root', J['arm_end_l'])]
arm_data = bpy.data.armatures.new('Steve3DRig')
arm = bpy.data.objects.new('Armature', arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
for name, parent, head in BONES:
    b = arm_data.edit_bones.new(name)
    b.head = head; b.tail = head + Vector((0, 0, 0.12 if name != 'root' else 0.08)); b.roll = 0.0
    if parent: b.parent = arm_data.edit_bones[parent]
    b.use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')

# ------------------------------------------------------------------ mesh
me = bpy.data.meshes.new('Steve3DMesh')
me.from_pydata([tuple(map(float, v)) for v in verts], [], [tuple(map(int, f)) for f in faces])
me.update()
uvl = me.uv_layers.new(name='UVMap')
flat = uvs.reshape(-1, 2)
uvl.data.foreach_set('uv', flat.ravel().astype(np.float32))
me.materials.append(bpy.data.materials.new('Base'))
for p in me.polygons: p.use_smooth = False
mesh = bpy.data.objects.new('Steve3D', me)
scene.collection.objects.link(mesh)
# ---- skin weights the Kratos way: Blender's automatic (bone heat) weights on the solid mesh, computed against a
# temporary rig whose bones lie along the limbs (shoulder->elbow->hand, hip->knee->ankle, pelvis->neck->crown);
# the groups then carry over by name to the animation rig. Vertices bone heat cannot solve keep the UV-region weights.
wdata = bpy.data.armatures.new('WeightRig'); wrig = bpy.data.objects.new('WeightRig', wdata); scene.collection.objects.link(wrig)
bpy.context.view_layer.objects.active = wrig
bpy.ops.object.mode_set(mode='EDIT')
SEG = {'body': (J['body'], J['head']), 'head': (J['head'], J['head'] + Vector((0, 0, 0.5))),
       'arm_r': (J['arm_r'], J['forearm_r']), 'forearm_r': (J['forearm_r'], J['arm_end_r']),
       'arm_l': (J['arm_l'], J['forearm_l']), 'forearm_l': (J['forearm_l'], J['arm_end_l']),
       'leg_r': (J['leg_r'], J['shin_r']), 'shin_r': (J['shin_r'], J['leg_end_r'] - Vector((0, 0, 0.1))),
       'leg_l': (J['leg_l'], J['shin_l']), 'shin_l': (J['shin_l'], J['leg_end_l'] - Vector((0, 0, 0.1)))}
PAR = {'head': 'body', 'arm_r': 'body', 'forearm_r': 'arm_r', 'arm_l': 'body', 'forearm_l': 'arm_l', 'leg_r': None, 'shin_r': 'leg_r', 'leg_l': None, 'shin_l': 'leg_l'}
for n, (h, t) in SEG.items():
    b = wdata.edit_bones.new(n); b.head = h; b.tail = t
for n, p in PAR.items():
    if p: wdata.edit_bones[n].parent = wdata.edit_bones[p]
bpy.ops.object.mode_set(mode='OBJECT')
for o in bpy.data.objects: o.select_set(o in (mesh, wrig))
bpy.context.view_layer.objects.active = wrig
heat_ok = True
try:
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
except Exception as ex:
    heat_ok = False; print('WARN bone heat failed:', ex)
# fill vertices without any weight from the UV-region weights
vw = np.zeros(len(verts))
for v in me.vertices:
    vw[v.index] = sum(g.weight for g in v.groups)
missing = np.nonzero(vw < 1e-3)[0]
for gi, gname in enumerate(names):
    g = mesh.vertex_groups.get(gname) or mesh.vertex_groups.new(name=gname)
    col = weights[missing, gi]
    for w in np.unique(np.round(col[col > 1e-4], 3)):
        idx = missing[np.round(col, 3) == w]
        g.add(idx.tolist(), float(w), 'REPLACE')
for side in ('r', 'l'):   # rigid block legs, also when bone heat produced shin weights
    gs, gl = mesh.vertex_groups.get('shin_' + side), mesh.vertex_groups.get('leg_' + side)
    if gs and gl:
        for v in me.vertices:
            for g in v.groups:
                if g.group == gs.index and g.weight > 0:
                    gl.add([v.index], g.weight, 'ADD'); gs.add([v.index], 0.0, 'REPLACE')
print('STEVE3D weights: bone heat %s, %d of %d vertices filled from UV regions' % ('ok' if heat_ok else 'FAILED', len(missing), len(verts)))
for m_ in list(mesh.modifiers): mesh.modifiers.remove(m_)
mesh.parent = None
bpy.data.objects.remove(wrig, do_unlink=True)
mesh.parent = arm
mod = mesh.modifiers.new('Armature', 'ARMATURE'); mod.object = arm
print('STEVE3D mesh verts=%d tris=%d groups=%s' % (len(verts), len(faces), names))

# ------------------------------------------------------------------ clips
for pb in arm.pose.bones: pb.rotation_mode = 'ZYX'
arm.animation_data_create()
D = math.radians
LIMBS = ('arm_r', 'forearm_r', 'arm_l', 'forearm_l', 'leg_r', 'shin_r', 'leg_l', 'shin_l', 'pelvis', 'body', 'head')
CLIPS = []
def add(name, length, keys, loop=False): CLIPS.append((name, length, keys, loop))
def P(**b): return b
def mix(*poses):
    out = {}
    for p in poses: out.update(p)
    return out

# Minecraft Steve has no knees: each leg is one rigid block (shins carry no weight and never rotate). Poses use hip
# swing, spread, body lean and arms. rot X > 0 swings a hanging limb back (negative = forward / up); Z < 0 is outward for
# right limbs, Z > 0 outward for left limbs.
REST = P(arm_l={'r': (0, 0, 4)}, arm_r={'r': (0, 0, -4)})
def pose(**over): return mix(REST, over)
def walk(a, lean=0, arm=None):
    arm = a if arm is None else arm
    return {0: pose(body={'r': (lean, 0, 0)}, leg_l={'r': (-a, 0, 2)}, leg_r={'r': (a, 0, -2)}, arm_l={'r': (arm, 0, 6)}, arm_r={'r': (-arm, 0, -6)}),
            'mid': pose(body={'r': (lean, 0, 0)}, pelvis={'l': (0, 0, 0.02)}, arm_l={'r': (0, 0, 6)}, arm_r={'r': (0, 0, -6)}),
            1: pose(body={'r': (lean, 0, 0)}, leg_l={'r': (a, 0, 2)}, leg_r={'r': (-a, 0, -2)}, arm_l={'r': (-arm, 0, 6)}, arm_r={'r': (arm, 0, -6)})}
def cycle(name, n, a, lean=0, arm=None):
    w = walk(a, lean, arm)
    add(name, n, {0: w[0], n // 4: w['mid'], n // 2: w[1], 3 * n // 4: w['mid'], n: w[0]}, loop=True)
add('Idle', 60, {0: pose(), 30: pose(body={'r': (1.5, 0, 0)}, head={'r': (0, 6, 0)}, arm_l={'r': (-3, 0, 6)}, arm_r={'r': (3, 0, -6)}), 60: pose()}, loop=True)
cycle('Walk', 24, 32)
cycle('Run', 16, 52, lean=12, arm=60)
add('Jump', 10, {0: pose(body={'r': (14, 0, 0)}, arm_l={'r': (25, 0, 10)}, arm_r={'r': (25, 0, -10)}),
                 4: pose(body={'r': (-6, 0, 0)}, arm_l={'r': (-150, 0, 18)}, arm_r={'r': (-150, 0, -18)}, leg_l={'r': (8, 0, 4)}, leg_r={'r': (-14, 0, -4)}),
                 10: pose(arm_l={'r': (-120, 0, 30)}, arm_r={'r': (-120, 0, -30)}, leg_l={'r': (-10, 0, 6)}, leg_r={'r': (12, 0, -6)})})
FALL = pose(body={'r': (4, 0, 0)}, arm_l={'r': (-100, 0, 55)}, arm_r={'r': (-100, 0, -55)}, leg_l={'r': (-14, 0, 12)}, leg_r={'r': (10, 0, -12)})
add('Fall', 20, {0: FALL, 10: mix(FALL, {'arm_l': {'r': (-120, 0, 65)}, 'arm_r': {'r': (-85, 0, -45)}, 'leg_l': {'r': (6, 0, 12)}, 'leg_r': {'r': (-12, 0, -12)}}), 20: FALL}, loop=True)
add('Land', 10, {0: pose(pelvis={'l': (0, 0, -0.03)}, body={'r': (26, 0, 0)}, head={'r': (-16, 0, 0)}, arm_l={'r': (-40, 0, 25)}, arm_r={'r': (-40, 0, -25)}, leg_l={'r': (0, 0, 10)}, leg_r={'r': (0, 0, -10)}),
                 10: pose()})
# Rolling ball: Steve tucks into a cube (Unreal spins the whole character about his centre from the real rolling speed)
TUCK = pose(pelvis={'l': (0, 0.0, 0.32)}, body={'r': (62, 0, 0)}, head={'r': (20, 0, 0)}, arm_l={'r': (-75, 0, -18)}, forearm_l={'r': (-40, 0, 0)},
            arm_r={'r': (-75, 0, 18)}, forearm_r={'r': (-40, 0, 0)}, leg_l={'r': (-95, 0, 4)}, leg_r={'r': (-95, 0, -4)})
add('Roll', 20, {0: TUCK, 10: mix(TUCK, {'body': {'r': (66, 0, 0)}}), 20: TUCK}, loop=True)
SLIDE = pose(pelvis={'l': (0, 0, -0.02)}, body={'r': (6, 18, -10)}, head={'r': (0, -14, 0)}, arm_l={'r': (-12, 0, 72)}, arm_r={'r': (-12, 0, -78)},
             leg_l={'r': (-12, 0, 18)}, leg_r={'r': (12, 0, -18)})
add('Slide', 20, {0: SLIDE, 10: mix(SLIDE, {'arm_l': {'r': (-16, 0, 66)}, 'arm_r': {'r': (-8, 0, -84)}, 'body': {'r': (6, 18, -6)}}), 20: SLIDE}, loop=True)
add('Launch', 12, {0: pose(body={'r': (-16, 0, 0)}, head={'r': (-14, 0, 0)}, arm_l={'r': (-170, 0, 22)}, arm_r={'r': (-170, 0, -22)}, leg_l={'r': (16, 0, 6)}, leg_r={'r': (22, 0, -6)}),
                   12: pose(body={'r': (-8, 0, 0)}, arm_l={'r': (-150, 0, 40)}, arm_r={'r': (-150, 0, -40)}, leg_l={'r': (10, 0, 8)}, leg_r={'r': (14, 0, -8)})})
add('PortalEnter', 10, {0: pose(body={'r': (20, 0, 0)}, arm_l={'r': (-90, 0, 10)}, arm_r={'r': (-90, 0, -10)}),
                        10: pose(body={'r': (40, 0, 0)}, head={'r': (-10, 0, 0)}, arm_l={'r': (-175, 0, 6)}, arm_r={'r': (-175, 0, -6)}, leg_l={'r': (20, 0, 2)}, leg_r={'r': (20, 0, -2)})})
add('PortalExit', 12, {0: pose(body={'r': (-10, 0, 0)}, arm_l={'r': (-60, 0, 80)}, arm_r={'r': (-60, 0, -80)}, leg_l={'r': (-10, 0, 14)}, leg_r={'r': (8, 0, -14)}),
                       12: pose(arm_l={'r': (-20, 0, 30)}, arm_r={'r': (-20, 0, -30)})})
add('HitReact', 10, {0: pose(), 3: pose(body={'r': (-22, 0, 6)}, head={'r': (-18, 10, 0)}, arm_l={'r': (-70, 0, 40)}, arm_r={'r': (-50, 0, -50)}), 10: pose()})


# ------------------------------------------------------------------ Minecraft x Elden Combat clips (E_*, same rig)
# Unreal plays attack clips phase-mapped to the server's frames: [0, .35] windup, [.35, .55] active (the hit), [.55, 1]
# recovery, so one clip fits every weapon class. Right hand holds the weapon, left arm carries the shield.
STANCE = pose(pelvis={'l': (0, 0, -0.03)}, body={'r': (8, 10, 0)}, head={'r': (-4, -10, 0)}, arm_r={'r': (-30, 0, -12)}, forearm_r={'r': (-55, 0, 0)},
              arm_l={'r': (-55, 0, 18)}, forearm_l={'r': (-45, 0, 0)}, leg_l={'r': (-14, 0, 6)}, leg_r={'r': (14, 0, -6)})
def st(**over): return mix(STANCE, over)
add('E_Idle', 60, {0: STANCE, 30: st(body={'r': (10, 10, 0)}, pelvis={'l': (0, 0, -0.04)}, arm_l={'r': (-58, 0, 18)}), 60: STANCE}, loop=True)
def ewalk(a, run=False):
    w = walk(a, 12 if run else 6, 50 if run else 18)
    keep = {'arm_r': {'r': (30, 0, -15) if run else (-30, 0, -12)}, 'forearm_r': {'r': (-40, 0, 0) if run else (-55, 0, 0)}}
    if not run: keep.update({'arm_l': {'r': (-55, 0, 18)}, 'forearm_l': {'r': (-45, 0, 0)}})
    return {k: mix(v, keep) for k, v in w.items()}
for nm, n, a, run in (('E_Walk', 24, 30, False), ('E_Run', 16, 50, True)):
    w = ewalk(a, run)
    add(nm, n, {0: w[0], n // 4: w['mid'], n // 2: w[1], 3 * n // 4: w['mid'], n: w[0]}, loop=True)
add('E_Jump', 10, {0: st(body={'r': (14, 0, 0)}), 4: st(body={'r': (-6, 0, 0)}, leg_l={'r': (8, 0, 4)}, leg_r={'r': (-14, 0, -4)}), 10: st(leg_l={'r': (-10, 0, 6)}, leg_r={'r': (12, 0, -6)})})
add('E_Fall', 20, {0: st(arm_l={'r': (-90, 0, 50)}, leg_l={'r': (-14, 0, 12)}, leg_r={'r': (10, 0, -12)}), 10: st(arm_l={'r': (-100, 0, 55)}, leg_l={'r': (6, 0, 12)}, leg_r={'r': (-12, 0, -12)}),
                   20: st(arm_l={'r': (-90, 0, 50)}, leg_l={'r': (-14, 0, 12)}, leg_r={'r': (10, 0, -12)})}, loop=True)
LUNGE = {'leg_l': {'r': (-30, 0, 6)}, 'leg_r': {'r': (22, 0, -6)}, 'pelvis': {'l': (0, 0, -0.07)}}
add('E_Light1', 30, {0: STANCE, 10: st(body={'r': (6, -28, 0)}, head={'r': (-4, 10, 0)}, arm_r={'r': (-100, 0, -72)}, forearm_r={'r': (-30, 0, 0)}),
                     16: mix(st(body={'r': (14, 30, 0)}, head={'r': (0, -20, 0)}, arm_r={'r': (-85, 0, 42)}, forearm_r={'r': (-8, 0, 0)}), LUNGE), 30: STANCE})
add('E_Light2', 30, {0: STANCE, 10: st(body={'r': (6, 30, 0)}, arm_r={'r': (-82, 0, 45)}, forearm_r={'r': (-75, 0, 0)}),
                     16: mix(st(body={'r': (14, -30, 0)}, head={'r': (0, 15, 0)}, arm_r={'r': (-95, 0, -78)}, forearm_r={'r': (-6, 0, 0)}), LUNGE), 30: STANCE})
add('E_Light3', 30, {0: STANCE, 10: st(body={'r': (-10, 5, 0)}, arm_r={'r': (-175, 0, -10)}, forearm_r={'r': (-30, 0, 0)}),
                     16: mix(st(body={'r': (26, 0, 0)}, head={'r': (-12, 0, 0)}, arm_r={'r': (-55, 0, -5)}, forearm_r={'r': (-4, 0, 0)}), LUNGE), 30: STANCE})
HEAVY_UP = st(body={'r': (-15, 0, 0)}, head={'r': (-10, 0, 0)}, arm_r={'r': (-170, 0, -8)}, forearm_r={'r': (-25, 0, 0)}, arm_l={'r': (-170, 0, 8)}, forearm_l={'r': (-25, 0, 0)})
add('E_Heavy', 36, {0: STANCE, 12: HEAVY_UP,
                    19: mix(st(body={'r': (36, 0, 0)}, head={'r': (-16, 0, 0)}, arm_r={'r': (-40, 0, -4)}, forearm_r={'r': (-2, 0, 0)}, arm_l={'r': (-40, 0, 4)}, forearm_l={'r': (-2, 0, 0)}),
                            {'leg_l': {'r': (-34, 0, 6)}, 'leg_r': {'r': (26, 0, -6)}, 'pelvis': {'l': (0, 0, -0.12)}}), 36: STANCE})
add('E_Charge', 20, {0: HEAVY_UP, 10: mix(HEAVY_UP, {'body': {'r': (-18, 2, 0)}, 'arm_r': {'r': (-174, 0, -10)}}), 20: HEAVY_UP}, loop=True)
add('E_Thrust', 30, {0: STANCE, 10: st(body={'r': (4, -22, 0)}, arm_r={'r': (-60, 0, -22)}, forearm_r={'r': (-105, 0, 0)}),
                     16: mix(st(body={'r': (18, 12, 0)}, arm_r={'r': (-92, 0, 2)}, forearm_r={'r': (0, 0, 0)}), LUNGE), 30: STANCE})
# dodge roll: the tucked body turns a full circle about its own centre (root offset keeps the centre at ball height)
TUCKE = mix(TUCK, {'pelvis': {'l': (0, 0, -0.3)}, 'arm_r': {'r': (-75, 0, 18)}, 'forearm_r': {'r': (-60, 0, 0)}})   # crouched ball, centre ~0.55 m
roll_keys = {0: STANCE}
R0 = 0.55
for f in range(2, 19, 2):
    th = (f - 2) / 16.0 * 360.0; tr = math.radians(th)
    roll_keys[f] = mix(TUCKE, {'root': {'r': (th, 0, 0), 'l': (0, -R0 * math.sin(tr), R0 * (1 - math.cos(tr)))}})
roll_keys[22] = mix(STANCE, {'root': {'r': (360, 0, 0)}})   # 360 = upright again (no spin back)
add('E_Dodge', 22, roll_keys)
add('E_Backstep', 14, {0: STANCE, 5: st(body={'r': (-18, 0, 0)}, head={'r': (10, 0, 0)}, arm_r={'r': (-60, 0, -25)}, arm_l={'r': (-70, 0, 30)}, leg_r={'r': (32, 0, -6)}, leg_l={'r': (-8, 0, 6)}),
                       14: STANCE})
BLOCK = st(body={'r': (12, 6, 0)}, pelvis={'l': (0, 0, -0.06)}, arm_l={'r': (-88, 0, -28)}, forearm_l={'r': (-62, 0, 0)}, arm_r={'r': (-18, 0, -10)}, forearm_r={'r': (-65, 0, 0)})
add('E_Block', 40, {0: BLOCK, 20: mix(BLOCK, {'body': {'r': (13, 6, 0)}}), 40: BLOCK}, loop=True)
add('E_BlockHit', 10, {0: BLOCK, 3: mix(BLOCK, {'body': {'r': (-8, 6, 0)}, 'arm_l': {'r': (-62, 0, -12)}, 'head': {'r': (-10, 0, 0)}}), 10: BLOCK})
add('E_Parry', 24, {0: BLOCK, 5: st(body={'r': (8, -15, 0)}, arm_l={'r': (-70, 0, -50)}, forearm_l={'r': (-30, 0, 0)}),
                    11: st(body={'r': (8, 25, 0)}, arm_l={'r': (-105, 0, 72)}, forearm_l={'r': (-10, 0, 0)}), 24: STANCE})
add('E_GuardBreak', 45, {0: BLOCK, 7: st(body={'r': (-30, 0, 0)}, head={'r': (-22, 0, 0)}, pelvis={'l': (0, 0, -0.06)}, arm_l={'r': (-140, 0, 60)}, arm_r={'r': (-120, 0, -60)}, leg_r={'r': (25, 0, -8)}),
                         25: st(body={'r': (-24, 0, 6)}, head={'r': (-14, 8, 0)}, arm_l={'r': (-120, 0, 55)}, arm_r={'r': (-100, 0, -55)}, leg_r={'r': (20, 0, -8)}), 45: STANCE})
add('E_Stagger', 21, {0: STANCE, 4: st(body={'r': (-26, 0, 8)}, head={'r': (-20, 12, 0)}, arm_l={'r': (-75, 0, 45)}, arm_r={'r': (-55, 0, -55)}, leg_l={'r': (8, 0, 8)}), 21: STANCE})
add('E_Riposte', 39, {0: STANCE, 12: mix(st(body={'r': (24, 0, 0)}, arm_r={'r': (-90, 0, 0)}, forearm_r={'r': (0, 0, 0)}), LUNGE),
                      19: mix(st(body={'r': (20, -30, 0)}, arm_r={'r': (-112, 0, -52)}, forearm_r={'r': (-20, 0, 0)}), LUNGE),
                      27: st(body={'r': (4, -10, 0)}, arm_r={'r': (-62, 0, -32)}, forearm_r={'r': (-40, 0, 0)}), 39: STANCE})
add('E_Death', 40, {0: STANCE, 12: st(body={'r': (32, 0, 0)}, head={'r': (22, 0, 0)}, pelvis={'l': (0, 0, -0.18)}, arm_r={'r': (-10, 0, -8)}, arm_l={'r': (-10, 0, 8)}),
                    30: mix(pose(arm_r={'r': (-165, 0, -10)}, arm_l={'r': (-165, 0, 10)}), {'root': {'r': (86, 0, 0), 'l': (0, 0, 0.12)}}),
                    40: mix(pose(arm_r={'r': (-165, 0, -10)}, arm_l={'r': (-165, 0, 10)}), {'root': {'r': (86, 0, 0), 'l': (0, 0, 0.12)}})})


# ------------------------------------------------------------------ Elden Ring Combat clips (R_*, same rig): a rewrite
# in the Elden Ring manner - bladed stance (left shoulder forward, weight on the back leg, shield up, sword low and
# forward), every attack with anticipation -> committed strike with a step -> follow-through overshoot -> slow
# recovery; hips lead the shoulders, the head stays on the target. Phase map as the E_ set: [0,.33] windup,
# [.33,.53] active, [.53,1] recovery (Unreal maps the server frames onto these keys).
RSTANCE = pose(pelvis={'r': (0, -18, 0), 'l': (0, 0, -0.06)}, body={'r': (7, -10, 0)}, head={'r': (-3, 26, 0)},
               arm_r={'r': (-28, 0, -16)}, forearm_r={'r': (-48, 0, 0)}, arm_l={'r': (-52, 0, -6)}, forearm_l={'r': (-62, 0, 0)},
               leg_l={'r': (-20, 0, 7)}, leg_r={'r': (17, 0, -7)})
def rs(**over): return mix(RSTANCE, over)
def rstep(fwd=0.08, down=-0.09, front=-34, back=24):   # a committed step into the swing (rigid Steve legs: hip swing)
    return {'pelvis': {'r': (0, 0, 0), 'l': (0, fwd, down)}, 'leg_l': {'r': (front, 0, 7)}, 'leg_r': {'r': (back, 0, -7)}}
add('R_Idle', 90, {0: RSTANCE,
                   30: rs(pelvis={'r': (0, -18, 0), 'l': (0, 0, -0.07)}, body={'r': (9, -10, 0)}, arm_l={'r': (-54, 0, -6)}, arm_r={'r': (-26, 0, -16)}),
                   60: rs(body={'r': (8, -11, 1)}, head={'r': (-2, 24, 0)}), 90: RSTANCE}, loop=True)
def rwalk(n, a, lean, swing, bob, run=False):
    keys = {}
    for i, ph in enumerate((0.0, 0.25, 0.5, 0.75)):
        f = int(round(ph * n)); sgn = 1 if ph < 0.5 else -1; mid = ph in (0.25, 0.75)
        legl = 0 if mid else -a * sgn; legr = -legl
        armr = (-28 if not run else 25) + (0 if mid else swing * sgn)
        keys[f] = rs(pelvis={'r': (0, -12 + (0 if mid else 4 * sgn), (0 if mid else 3 * sgn)), 'l': (0, 0, -0.04 + (bob if mid else 0))},
                     body={'r': (lean, -8 + (0 if mid else -5 * sgn), 0)}, head={'r': (-3, 20 + (0 if mid else 4 * sgn), 0)},
                     leg_l={'r': (legl, 0, 6)}, leg_r={'r': (legr, 0, -6)},
                     arm_r={'r': (armr, 0, -16)}, forearm_r={'r': (-48 if not run else -70, 0, 0)},
                     arm_l={'r': ((-52 if not run else -20 - (0 if mid else swing * sgn)), 0, -6)}, forearm_l={'r': ((-62 if not run else -80), 0, 0)})
    keys[n] = keys[0]
    return keys
add('R_Walk', 32, rwalk(32, 26, 8, 8, 0.02), loop=True)
add('R_Run', 20, rwalk(20, 46, 16, 28, 0.04, run=True), loop=True)
add('R_Jump', 12, {0: rs(pelvis={'l': (0, 0, -0.12)}, body={'r': (18, -6, 0)}), 5: rs(body={'r': (-4, -6, 0)}, leg_l={'r': (-30, 0, 6)}, leg_r={'r': (10, 0, -6)}),
                   12: rs(leg_l={'r': (-18, 0, 8)}, leg_r={'r': (14, 0, -8)}, arm_l={'r': (-70, 0, 20)})})
add('R_Fall', 24, {0: rs(arm_l={'r': (-80, 0, 35)}, arm_r={'r': (-60, 0, -40)}, leg_l={'r': (-16, 0, 12)}, leg_r={'r': (12, 0, -12)}, body={'r': (2, -6, 0)}),
                   12: rs(arm_l={'r': (-90, 0, 40)}, arm_r={'r': (-70, 0, -45)}, leg_l={'r': (-6, 0, 12)}, leg_r={'r': (4, 0, -12)}, body={'r': (4, -6, 0)})}, loop=True)
# R1 #1: diagonal cut, upper right -> lower left
add('R_Light1', 36, {0: RSTANCE,
    6: rs(pelvis={'r': (0, -30, 0), 'l': (0, -0.02, -0.08)}, body={'r': (2, -32, 0)}, head={'r': (-4, 40, 0)}, arm_r={'r': (-118, 0, -58)}, forearm_r={'r': (-52, 0, 0)}, leg_r={'r': (22, 0, -7)}),
    12: rs(pelvis={'r': (0, -34, 0), 'l': (0, -0.03, -0.08)}, body={'r': (0, -38, 0)}, head={'r': (-4, 44, 0)}, arm_r={'r': (-135, 0, -70)}, forearm_r={'r': (-55, 0, 0)}, leg_r={'r': (24, 0, -7)}),
    15: mix(rs(body={'r': (10, -2, 0)}, head={'r': (-4, 10, 0)}, arm_r={'r': (-98, 0, -8)}, forearm_r={'r': (-18, 0, 0)}), rstep()),
    19: mix(rs(body={'r': (18, 34, 0)}, head={'r': (-6, -12, 0)}, arm_r={'r': (-55, 0, 46)}, forearm_r={'r': (-6, 0, 0)}, arm_l={'r': (-40, 0, 10)}), rstep()),
    24: mix(rs(body={'r': (16, 30, 0)}, head={'r': (-6, -8, 0)}, arm_r={'r': (-50, 0, 40)}, forearm_r={'r': (-10, 0, 0)}), rstep(0.07, -0.08)),
    36: RSTANCE})
# R1 #2: horizontal backhand, left -> right
add('R_Light2', 36, {0: RSTANCE,
    6: rs(body={'r': (4, 28, 0)}, head={'r': (-4, -8, 0)}, arm_r={'r': (-84, 0, 48)}, forearm_r={'r': (-82, 0, 0)}, pelvis={'r': (0, 10, 0), 'l': (0, 0, -0.07)}),
    12: rs(body={'r': (4, 36, 0)}, head={'r': (-4, -14, 0)}, arm_r={'r': (-90, 0, 56)}, forearm_r={'r': (-92, 0, 0)}, pelvis={'r': (0, 14, 0), 'l': (0, 0, -0.07)}),
    15: mix(rs(body={'r': (10, 2, 0)}, arm_r={'r': (-92, 0, 4)}, forearm_r={'r': (-30, 0, 0)}), rstep()),
    19: mix(rs(body={'r': (12, -40, 0)}, head={'r': (-4, 40, 0)}, arm_r={'r': (-88, 0, -82)}, forearm_r={'r': (-4, 0, 0)}), rstep()),
    24: mix(rs(body={'r': (10, -36, 0)}, head={'r': (-4, 38, 0)}, arm_r={'r': (-80, 0, -76)}, forearm_r={'r': (-8, 0, 0)}), rstep(0.07, -0.08)),
    36: RSTANCE})
# R1 #3: overhead cleave
add('R_Light3', 36, {0: RSTANCE,
    6: rs(body={'r': (-10, -12, 0)}, arm_r={'r': (-158, 0, -14)}, forearm_r={'r': (-42, 0, 0)}, pelvis={'r': (0, -18, 0), 'l': (0, -0.02, -0.03)}),
    12: rs(body={'r': (-16, -12, 0)}, head={'r': (-10, 24, 0)}, arm_r={'r': (-176, 0, -10)}, forearm_r={'r': (-58, 0, 0)}, pelvis={'r': (0, -18, 0), 'l': (0, -0.03, -0.02)}),
    15: mix(rs(body={'r': (8, -4, 0)}, arm_r={'r': (-112, 0, -6)}, forearm_r={'r': (-22, 0, 0)}), rstep(0.1, -0.1)),
    19: mix(rs(body={'r': (34, 0, 0)}, head={'r': (-18, 6, 0)}, arm_r={'r': (-38, 0, -2)}, forearm_r={'r': (-4, 0, 0)}), rstep(0.12, -0.12, -40, 28)),
    24: mix(rs(body={'r': (30, 0, 0)}, head={'r': (-16, 6, 0)}, arm_r={'r': (-42, 0, -2)}, forearm_r={'r': (-6, 0, 0)}), rstep(0.11, -0.11, -38, 26)),
    36: RSTANCE})
# R2: two-handed overhead smash with a big gather and a planted impact
R2_UP = rs(pelvis={'r': (0, -38, 0), 'l': (0, -0.04, -0.05)}, body={'r': (-16, -46, 0)}, head={'r': (-6, 50, 0)},
           arm_r={'r': (-168, 0, -30)}, forearm_r={'r': (-70, 0, 0)}, arm_l={'r': (-150, 0, -42)}, forearm_l={'r': (-55, 0, 0)}, leg_r={'r': (28, 0, -8)})
add('R_Heavy', 45, {0: RSTANCE,
    8: rs(pelvis={'r': (0, -30, 0), 'l': (0, -0.03, -0.07)}, body={'r': (-8, -36, 0)}, head={'r': (-4, 42, 0)}, arm_r={'r': (-140, 0, -34)}, forearm_r={'r': (-74, 0, 0)}, arm_l={'r': (-122, 0, -46)}, forearm_l={'r': (-60, 0, 0)}),
    15: R2_UP,
    20: mix(rs(body={'r': (14, -10, 0)}, arm_r={'r': (-104, 0, -10)}, forearm_r={'r': (-24, 0, 0)}, arm_l={'r': (-100, 0, -25)}, forearm_l={'r': (-20, 0, 0)}), rstep(0.1, -0.12)),
    24: mix(rs(body={'r': (46, 4, 0)}, head={'r': (-24, 0, 0)}, arm_r={'r': (-30, 0, -6)}, forearm_r={'r': (-2, 0, 0)}, arm_l={'r': (-32, 0, -14)}, forearm_l={'r': (-2, 0, 0)}), rstep(0.14, -0.2, -44, 32)),
    31: mix(rs(body={'r': (44, 4, 0)}, head={'r': (-22, 0, 0)}, arm_r={'r': (-28, 0, -6)}, forearm_r={'r': (-4, 0, 0)}, arm_l={'r': (-30, 0, -14)}, forearm_l={'r': (-4, 0, 0)}), rstep(0.14, -0.2, -44, 32)),
    45: RSTANCE})
add('R_Charge', 24, {0: R2_UP, 8: mix(R2_UP, {'body': {'r': (-18, -48, 1)}, 'arm_r': {'r': (-171, 0, -31)}}), 16: mix(R2_UP, {'body': {'r': (-17, -47, -1)}}), 24: R2_UP}, loop=True)
add('R_Thrust', 36, {0: RSTANCE,
    6: rs(body={'r': (4, -26, 0)}, arm_r={'r': (-64, 0, -26)}, forearm_r={'r': (-100, 0, 0)}, pelvis={'r': (0, -26, 0), 'l': (0, -0.03, -0.08)}),
    12: rs(body={'r': (4, -32, 0)}, arm_r={'r': (-70, 0, -28)}, forearm_r={'r': (-112, 0, 0)}, pelvis={'r': (0, -30, 0), 'l': (0, -0.04, -0.08)}),
    15: mix(rs(body={'r': (10, -6, 0)}, arm_r={'r': (-86, 0, -8)}, forearm_r={'r': (-40, 0, 0)}), rstep()),
    19: mix(rs(body={'r': (18, 12, 0)}, head={'r': (-6, 4, 0)}, arm_r={'r': (-92, 0, 4)}, forearm_r={'r': (0, 0, 0)}), rstep(0.14, -0.11, -42, 30)),
    24: mix(rs(body={'r': (16, 10, 0)}, arm_r={'r': (-90, 0, 4)}, forearm_r={'r': (-4, 0, 0)}), rstep(0.13, -0.1, -40, 28)),
    36: RSTANCE})
# roll: crouch, full forward roll about the ball's centre, kneeling recovery
RTUCK = mix(TUCK, {'pelvis': {'l': (0, 0, -0.3)}, 'arm_r': {'r': (-80, 0, 20)}, 'forearm_r': {'r': (-70, 0, 0)}, 'arm_l': {'r': (-80, 0, -20)}, 'forearm_l': {'r': (-70, 0, 0)}})
rkeys = {0: RSTANCE, 2: rs(body={'r': (32, -4, 0)}, pelvis={'r': (0, -6, 0), 'l': (0, 0.05, -0.16)}, arm_r={'r': (-60, 0, -10)}, arm_l={'r': (-60, 0, 10)})}
for f in range(3, 18):
    th = (f - 3) / 14.0 * 360.0; tr = math.radians(th)
    rkeys[f] = mix(RTUCK, {'root': {'r': (th, 0, 0), 'l': (0, -0.55 * math.sin(tr), 0.55 * (1 - math.cos(tr)))}})
rkeys[18] = mix(rs(body={'r': (28, -6, 0)}, pelvis={'r': (0, -10, 0), 'l': (0, 0.04, -0.2)}, leg_l={'r': (-40, 0, 10)}, leg_r={'r': (30, 0, -10)}), {'root': {'r': (360, 0, 0)}})
rkeys[24] = mix(RSTANCE, {'root': {'r': (360, 0, 0)}})
add('R_Dodge', 24, rkeys)
add('R_Backstep', 16, {0: RSTANCE, 4: rs(body={'r': (-14, -8, 0)}, head={'r': (6, 22, 0)}, pelvis={'r': (0, -14, 0), 'l': (0, -0.05, 0.03)}, leg_l={'r': (-8, 0, 7)}, leg_r={'r': (28, 0, -7)}, arm_l={'r': (-70, 0, -10)}),
                       9: rs(body={'r': (12, -10, 0)}, pelvis={'r': (0, -18, 0), 'l': (0, 0, -0.1)}), 16: RSTANCE})
RBLOCK = rs(pelvis={'r': (0, -8, 0), 'l': (0, 0, -0.1)}, body={'r': (14, 8, 0)}, head={'r': (-2, 6, 0)},
            arm_l={'r': (-96, 0, -36)}, forearm_l={'r': (-76, 0, 0)}, arm_r={'r': (8, 0, -26)}, forearm_r={'r': (-72, 0, 0)},
            leg_l={'r': (-24, 0, 10)}, leg_r={'r': (22, 0, -10)})
add('R_Block', 60, {0: RBLOCK, 30: mix(RBLOCK, {'body': {'r': (15, 8, 0)}, 'pelvis': {'r': (0, -8, 0), 'l': (0, 0, -0.11)}}), 60: RBLOCK}, loop=True)
add('R_BlockHit', 12, {0: RBLOCK, 3: mix(RBLOCK, {'body': {'r': (-4, 6, 0)}, 'head': {'r': (-12, 6, 0)}, 'pelvis': {'r': (0, -8, 0), 'l': (0, -0.07, -0.08)}, 'arm_l': {'r': (-84, 0, -20)}}), 12: RBLOCK})
add('R_Parry', 30, {0: RBLOCK, 4: rs(body={'r': (8, -16, 0)}, arm_l={'r': (-70, 0, -62)}, forearm_l={'r': (-40, 0, 0)}, pelvis={'r': (0, -24, 0), 'l': (0, 0, -0.08)}),
                    9: rs(body={'r': (8, 30, 0)}, head={'r': (-2, -4, 0)}, arm_l={'r': (-102, 0, 82)}, forearm_l={'r': (-8, 0, 0)}, pelvis={'r': (0, 6, 0), 'l': (0, 0, -0.08)}),
                    14: rs(body={'r': (8, 28, 0)}, arm_l={'r': (-98, 0, 78)}, forearm_l={'r': (-10, 0, 0)}), 30: RSTANCE})
add('R_GuardBreak', 50, {0: RBLOCK, 4: rs(body={'r': (-34, 0, 4)}, head={'r': (-26, 8, 0)}, pelvis={'r': (0, -10, 0), 'l': (0, -0.08, -0.04)}, arm_l={'r': (-150, 0, 72)}, arm_r={'r': (-128, 0, -62)}, leg_r={'r': (32, 0, -8)}),
                         20: rs(body={'r': (-22, 0, 9)}, head={'r': (-16, 12, 0)}, arm_l={'r': (-120, 0, 60)}, arm_r={'r': (-100, 0, -55)}, leg_r={'r': (26, 0, -8)}),
                         35: rs(body={'r': (22, -4, 0)}, head={'r': (10, 10, 0)}, arm_l={'r': (-20, 0, 12)}, arm_r={'r': (-12, 0, -12)}, pelvis={'r': (0, -12, 0), 'l': (0, 0, -0.1)}), 50: RSTANCE})
add('R_Stagger', 24, {0: RSTANCE, 4: rs(body={'r': (-28, 0, 10)}, head={'r': (-22, 14, 0)}, arm_l={'r': (-74, 0, 44)}, arm_r={'r': (-56, 0, -52)}, pelvis={'r': (0, -10, 0), 'l': (0, -0.06, -0.05)}),
                      12: rs(body={'r': (16, -4, 0)}, head={'r': (8, 14, 0)}, pelvis={'r': (0, -14, 0), 'l': (0, 0, -0.09)}), 24: RSTANCE})
add('R_Riposte', 50, {0: RSTANCE, 8: rs(body={'r': (6, -30, 0)}, arm_r={'r': (-70, 0, -26)}, forearm_r={'r': (-104, 0, 0)}, pelvis={'r': (0, -28, 0), 'l': (0, -0.03, -0.08)}),
                      14: mix(rs(body={'r': (22, 0, 0)}, arm_r={'r': (-92, 0, 0)}, forearm_r={'r': (0, 0, 0)}), rstep(0.14, -0.12, -42, 30)),
                      26: mix(rs(body={'r': (6, -26, 0)}, head={'r': (-14, 26, 0)}, arm_r={'r': (-132, 0, -30)}, forearm_r={'r': (-18, 0, 0)}), rstep(0.12, -0.1, -40, 28)),
                      34: rs(body={'r': (-6, -12, 0)}, arm_r={'r': (-62, 0, -42)}, forearm_r={'r': (-42, 0, 0)}, leg_l={'r': (-30, 0, 8)}), 50: RSTANCE})
add('R_Death', 60, {0: RSTANCE, 10: rs(body={'r': (-16, -6, 0)}, head={'r': (-14, 10, 0)}, arm_r={'r': (-30, 0, -20)}, arm_l={'r': (-30, 0, 20)}),
                    25: pose(pelvis={'l': (0, 0, -0.32)}, body={'r': (30, 0, 0)}, head={'r': (24, 0, 0)}, leg_l={'r': (-70, 0, 6)}, leg_r={'r': (-70, 0, -6)}, arm_r={'r': (-8, 0, -6)}, arm_l={'r': (-8, 0, 6)}),
                    45: mix(pose(arm_r={'r': (-165, 0, -10)}, arm_l={'r': (-165, 0, 10)}), {'root': {'r': (86, 0, 0), 'l': (0, 0, 0.12)}}),
                    60: mix(pose(arm_r={'r': (-165, 0, -10)}, arm_l={'r': (-165, 0, 10)}), {'root': {'r': (86, 0, 0), 'l': (0, 0, 0.12)}})})


def mirror_pose(p):
    """Reflect a pose across the character's centre plane: left <-> right bones, rot Y / Z and loc X negate."""
    out = {}
    for b, k in p.items():
        nb = b[:-2] + ('_l' if b.endswith('_r') else '_r') if (b.endswith('_r') or b.endswith('_l')) else b
        r = k.get('r', (0, 0, 0)); l = k.get('l', (0, 0, 0))
        out[nb] = {'r': (r[0], -r[1], -r[2]), 'l': (-l[0], l[1], l[2])}
    return out


def clip(name, length, keys, loop):
    # (mirror_pose is available for left-handed variants; the E_ / R_ sets are right-handed as authored)
    act = bpy.data.actions.new(name if name[:2] in ('E_', 'R_') else 'P_' + name)
    arm.animation_data.action = act
    if loop and length not in keys: keys[length] = keys[min(keys)]
    for f in sorted(keys):
        pose = keys[f]
        for pb in arm.pose.bones:
            k = {} if pb.name in ('shin_r', 'shin_l') else pose.get(pb.name, {})   # rigid block legs: no knee
            r = k.get('r', (0, 0, 0)); l = k.get('l', (0, 0, 0))
            pb.rotation_euler = (D(r[0]), D(r[1]), D(r[2]))
            pb.location = (l[0], l[2], l[1])        # bone local: X, Y = up, Z = forward ; l = (x, forward, up) metres
            pb.keyframe_insert('rotation_euler', frame=f); pb.keyframe_insert('location', frame=f)
        # IK helper bones track the hand / foot ends of this frame's FK pose (armature space)
        bpy.context.view_layer.update()
        for ik, src, end in (('ik_hand_r', 'forearm_r', 'arm_end_r'), ('ik_hand_l', 'forearm_l', 'arm_end_l'),
                             ('ik_foot_r', 'shin_r', 'leg_end_r'), ('ik_foot_l', 'shin_l', 'leg_end_l')):
            ps = arm.pose.bones[src]
            at = ps.matrix @ (ps.bone.matrix_local.inverted() @ J[end])
            d = at - J[end]
            pik = arm.pose.bones[ik]; pik.rotation_euler = (0, 0, 0); pik.location = (d.x, d.z, -d.y)
            pik.keyframe_insert('location', frame=f); pik.keyframe_insert('rotation_euler', frame=f)
    for fc in act.fcurves:
        for kp in fc.keyframe_points: kp.interpolation = 'BEZIER'
    act['crb_len'] = length
    return act


FBX = dict(apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL', axis_forward='-Z', axis_up='Y', add_leaf_bones=False,
           use_armature_deform_only=False, primary_bone_axis='Y', secondary_bone_axis='X', mesh_smooth_type='FACE',
           use_mesh_modifiers=False, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
           bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, path_mode='STRIP')


def export(path, objs, anim):
    for o in bpy.data.objects: o.select_set(o in objs)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'ARMATURE'} if anim else {'ARMATURE', 'MESH'},
                             global_scale=100.0, bake_anim=anim, **FBX)


arm.animation_data.action = None
export(os.path.join(fbx_dir, 'Steve3D.fbx'), [arm, mesh], False)
print('EXPORTED Steve3D.fbx')
report = {'fps': FPS, 'clips': {}}
for name, length, keys, loop in CLIPS:
    clip(name, length, keys, loop)
    scene.frame_start, scene.frame_end = 0, length
    fn = name if name[:2] in ('E_', 'R_') else 'P_' + name
    export(os.path.join(fbx_dir, fn + '.fbx'), [arm], True)
    report['clips'][name] = {'frames': length, 'loop': loop}
    print('EXPORTED %s.fbx (%d frames)' % (fn, length))
lines = sorted(('%s>%s' % (b.name, b.parent.name if b.parent else '')).lower() for b in arm.data.bones)
crc = zlib.crc32(''.join(l + '\n' for l in lines).encode('utf-8')) & 0xffffffff
json.dump({'bones': [b.name for b in arm.data.bones], 'crc': '%08X' % crc}, open(os.path.join(out, 'skeleton.json'), 'w'), indent=1)
json.dump(report, open(os.path.join(out, 'clips.json'), 'w'), indent=1)
print('SKELETON crc %08X bones %d' % (crc, len(arm.data.bones)))

# ------------------------------------------------------------------ previews
m = me.materials[0]; m.use_nodes = True
nt = m.node_tree; bsdf = nt.nodes.get('Principled BSDF')
t = nt.nodes.new('ShaderNodeTexImage'); t.image = skin; t.interpolation = 'Closest'
nt.links.new(t.outputs['Color'], bsdf.inputs['Base Color'])
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'; scene.display.shading.color_type = 'TEXTURE'
scene.render.resolution_x, scene.render.resolution_y = 480, 640
scene.world = bpy.data.worlds.new('w'); scene.world.color = (0.08, 0.08, 0.1)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 2.3
def shoot(fname, ang):
    dv = Vector((0, -1, 0)); dv.rotate(Matrix.Rotation(math.radians(ang), 3, 'Z'))
    cam.location = Vector((0, 0, 0.9)) + dv * 10.0
    cam.rotation_euler = (-dv).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(out, fname)
    bpy.ops.render.render(write_still=True)
arm.animation_data.action = None
for pb in arm.pose.bones: pb.rotation_euler = (0, 0, 0); pb.location = (0, 0, 0)
bpy.context.view_layer.update()
shoot('preview_rest_front.png', 0); shoot('preview_rest_side.png', 90)
for name, frac in (('Walk', 0.0), ('Run', 0.0), ('Jump', 0.4), ('Fall', 0.0), ('Roll', 0.0), ('Slide', 0.0), ('Launch', 0.0), ('PortalEnter', 1.0)):
    act = bpy.data.actions.get('P_' + name)
    arm.animation_data.action = act
    scene.frame_set(int(act['crb_len'] * frac))
    shoot('pose_%s.png' % name, 0)
for name, frac, ang in (('E_Idle', 0.0, 30), ('E_Light1', 10 / 30, 30), ('E_Light1', 16 / 30, 30), ('E_Light3', 10 / 30, 90), ('E_Heavy', 19 / 36, 90),
                        ('E_Thrust', 16 / 30, 90), ('E_Dodge', 10 / 22, 90), ('E_Block', 0.0, 30), ('E_Parry', 11 / 24, 30), ('E_GuardBreak', 7 / 45, 30),
                        ('E_Riposte', 19 / 39, 30), ('E_Death', 1.0, 90), ('E_Run', 0.0, 90),
                        ('R_Idle', 0.0, 30), ('R_Light1', 12 / 36, 30), ('R_Light1', 19 / 36, 30), ('R_Light2', 12 / 36, 30), ('R_Light2', 19 / 36, 30),
                        ('R_Light3', 12 / 36, 90), ('R_Light3', 19 / 36, 90), ('R_Heavy', 15 / 45, 90), ('R_Heavy', 24 / 45, 90), ('R_Thrust', 19 / 36, 90),
                        ('R_Dodge', 10 / 24, 90), ('R_Block', 0.0, 30), ('R_Parry', 9 / 30, 30), ('R_GuardBreak', 4 / 50, 30), ('R_Riposte', 26 / 50, 30),
                        ('R_Death', 1.0, 90), ('R_Run', 0.0, 90), ('R_Walk', 0.0, 90)):
    act = bpy.data.actions.get(name)
    arm.animation_data.action = act
    scene.frame_set(int(round(act['crb_len'] * frac)))
    shoot('ec_%s_%02d.png' % (name, int(frac * 100)), ang)
print('DONE')
