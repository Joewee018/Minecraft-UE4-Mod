"""God of War Unity port - asset build (Blender 3.6, headless). PRIVATE BUILD: uses the repo's Kratos and Leviathan Axe
models, which the repo does not license; the user keeps this build private.
  blender -b --factory-startup -P gow_build.py -- <repo_dir> <out_dir> [player_height_m]

Outputs in <out_dir>:
  fbx/Avatar.fbx        Kratos (Epic/Fortnite skeleton) - the player              -> SK_Avatar
  fbx/Mutant.fbx        the Mixamo Mutant - the enemy                             -> SK_Mutant
  fbx/LeviathanAxe.fbx  the axe, re-framed: grip at the origin, haft +Z, blade +X  -> SM_LeviathanAxe
  fbx/A_<Role>.fbx      player clips: the repo's Kratos Mixamo clips (+ Mutant Dying as Death) retargeted onto the
                        Epic skeleton (name map + rest-direction alignment + world rotation deltas + scaled hips travel)
  fbx/E_<Role>.fbx      enemy clips on the Mutant skeleton (Idle, Swiping, Dying)
  T_*.png               textures (axe maps downsized to 2048)
  skeleton.json, clips.json, preview_*.png, pose_*.png"""
import bpy, json, math, os, shutil, sys, zlib
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
ref, out = argv[0], argv[1]
player_h = float(argv[2]) if len(argv) > 2 else 1.90
enemy_h = 2.05
fbx_dir = os.path.join(out, 'fbx'); os.makedirs(fbx_dir, exist_ok=True)
AS = os.path.join(ref, 'Assets'); A = os.path.join(AS, 'Animations')
M = 'mixamorig:'

PLAYER_CLIPS = {
    'Idle': 'Kratos/Standing Idle.fbx',
    'WalkF': 'Kratos/Standing Walk Forward.fbx', 'WalkB': 'Kratos/Standing Walk Back.fbx',
    'WalkL': 'Kratos/Standing Walk Left.fbx', 'WalkR': 'Kratos/Standing Walk Right.fbx',
    'RunF': 'Kratos/Standing Run Forward.fbx', 'RunB': 'Kratos/Standing Run Back.fbx',
    'JogL': 'Kratos/Jog Strafe Left.fbx', 'JogR': 'Kratos/Jog Strafe Right.fbx',
    'Attack': 'Kratos/Standing Melee Attack Downward.fbx',
    'Death': 'Mutant/Mutant Dying.fbx',
}
ENEMY_CLIPS = {'Swipe': 'Mutant/Mutant Swiping.fbx', 'EnemyDeath': 'Mutant/Mutant Dying.fbx'}

# Epic (Fortnite) bone <- Mixamo bone
MAP = {'pelvis': 'Hips', 'spine_01': 'Spine', 'spine_03': 'Spine1', 'spine_05': 'Spine2', 'neck_01': 'Neck', 'head': 'Head'}
for s, S in (('l', 'Left'), ('r', 'Right')):
    MAP.update({'clavicle_' + s: S + 'Shoulder', 'upperarm_' + s: S + 'Arm', 'lowerarm_' + s: S + 'ForeArm', 'hand_' + s: S + 'Hand',
                'thigh_' + s: S + 'UpLeg', 'calf_' + s: S + 'Leg', 'foot_' + s: S + 'Foot', 'ball_' + s: S + 'ToeBase'})
    for f, F in (('thumb', 'Thumb'), ('index', 'Index'), ('middle', 'Middle'), ('ring', 'Ring'), ('pinky', 'Pinky')):
        for i in (1, 2, 3):
            MAP['%s_%02d_%s' % (f, i, s)] = '%s%s%d' % (S + 'Hand', F, i)
# aim child used to align rest directions (target bone -> next target bone in its chain)
AIM = {'pelvis': 'spine_01', 'spine_01': 'spine_03', 'spine_03': 'spine_05', 'spine_05': 'neck_01', 'neck_01': 'head'}
for s in ('l', 'r'):
    AIM.update({'clavicle_' + s: 'upperarm_' + s, 'upperarm_' + s: 'lowerarm_' + s, 'lowerarm_' + s: 'hand_' + s, 'hand_' + s: 'middle_01_' + s,
                'thigh_' + s: 'calf_' + s, 'calf_' + s: 'foot_' + s, 'foot_' + s: 'ball_' + s})
    for f in ('thumb', 'index', 'middle', 'ring', 'pinky'):
        AIM['%s_01_%s' % (f, s)] = '%s_02_%s' % (f, s); AIM['%s_02_%s' % (f, s)] = '%s_03_%s' % (f, s)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False, ignore_leaf_bones=False)
    objs = [o for o in bpy.data.objects if o not in before]
    arm = next((o for o in objs if o.type == 'ARMATURE'), None)
    s = arm.scale.x if arm else 1.0
    for o in bpy.data.objects: o.select_set(o in objs)
    bpy.context.view_layer.objects.active = arm or objs[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    act = arm.animation_data.action if arm and arm.animation_data else None
    if act and abs(s - 1.0) > 1e-6:      # pose-bone location keys are bone-local: scale them by hand
        for fc in act.fcurves:
            if fc.data_path.endswith('.location'):
                for k in fc.keyframe_points:
                    k.co[1] *= s; k.handle_left[1] *= s; k.handle_right[1] *= s
    return arm, objs, act


def bounds(objs):
    ws = [o.matrix_world @ v.co for o in objs for v in o.data.vertices]
    lo = Vector((min(w.x for w in ws), min(w.y for w in ws), min(w.z for w in ws)))
    hi = Vector((max(w.x for w in ws), max(w.y for w in ws), max(w.z for w in ws)))
    return lo, hi


FBX = dict(apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL', axis_forward='-Z', axis_up='Y', add_leaf_bones=False,
           use_armature_deform_only=False, primary_bone_axis='Y', secondary_bone_axis='X', mesh_smooth_type='FACE',
           use_mesh_modifiers=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
           bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, path_mode='STRIP')


def export(path, objs, anim, scale, types=None):
    for o in bpy.data.objects: o.select_set(o in objs)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types=types or ({'ARMATURE'} if anim else {'ARMATURE', 'MESH'}),
                             global_scale=scale * 100.0, bake_anim=anim, **FBX)   # FBX_SCALE_ALL writes cm units: metres x100


def crc_of(arm):
    # lower-case: a packaged UE build prints FNames in the case they were first registered with
    lines = sorted(('%s>%s' % (b.name, b.parent.name if b.parent else '')).lower() for b in arm.data.bones)
    return zlib.crc32(''.join(l + '\n' for l in lines).encode('utf-8')) & 0xffffffff


def ensure_anim(o):
    if not o.animation_data: o.animation_data_create()


def rename(obj, name):
    """Give obj the exact name (Unreal skips an FBX armature node called 'Armature'); park any holder elsewhere."""
    other = bpy.data.objects.get(name)
    if other is not None and other is not obj: other.name = name + '_parked'
    obj.name = name


# ================= player: Kratos =================
kra, kra_objs, _ = import_fbx(os.path.join(AS, 'Kratos', 'KRATOS FORTNITE.fbx'))
kra_meshes = [o for o in kra_objs if o.type == 'MESH']
for o in kra_objs:
    if o.type not in ('MESH', 'ARMATURE'): bpy.data.objects.remove(o, do_unlink=True)
kra.data.pose_position = 'REST'; bpy.context.view_layer.update()
lo, hi = bounds(kra_meshes)
kk = player_h / (hi.z - lo.z)
print('KRATOS meshes=%d verts=%d bones=%d height=%.3f m -> scale %.4f' % (len(kra_meshes), sum(len(m.data.vertices) for m in kra_meshes), len(kra.data.bones), hi.z - lo.z, kk))
print('KRATOS materials', sorted({s.material.name for m in kra_meshes for s in m.material_slots if s.material}))
json.dump({'bones': [b.name for b in kra.data.bones], 'bone_count': len(kra.data.bones), 'skeleton_crc32': crc_of(kra), 'height_m': hi.z - lo.z, 'scale': kk},
          open(os.path.join(out, 'skeleton.json'), 'w'), indent=1)
print('SKELETON player bones=%d crc32=%08X' % (len(kra.data.bones), crc_of(kra)))
rename(kra, 'Armature')
export(os.path.join(fbx_dir, 'Avatar.fbx'), [kra] + kra_meshes, False, kk)
rename(kra, 'KratosRig')
print('EXPORTED Avatar.fbx (Kratos)')
tex = os.path.join(AS, 'Kratos', 'Textures')
for src, dst in (('body_d.png', 'T_Kratos_Body_D.png'), ('head_d.png', 'T_Kratos_Head_D.png'), ('beard_d.png', 'T_Kratos_Beard_D.png')):
    if os.path.exists(os.path.join(tex, src)): shutil.copyfile(os.path.join(tex, src), os.path.join(out, dst))

# ================= enemy: Mutant =================
mut, mut_objs, mut_idle = import_fbx(os.path.join(A, 'Mutant', 'Idle.fbx'))
mut_meshes = [o for o in mut_objs if o.type == 'MESH']
mut.data.pose_position = 'REST'; bpy.context.view_layer.update()
mlo, mhi = bounds(mut_meshes)
km = enemy_h / (mhi.z - mlo.z)
rename(mut, 'Armature')
export(os.path.join(fbx_dir, 'Mutant.fbx'), [mut] + mut_meshes, False, km)
print('EXPORTED Mutant.fbx (%.3f m -> scale %.4f)' % (mhi.z - mlo.z, km))
mut.data.pose_position = 'POSE'
ensure_anim(mut)
for src, dst in (('Mutant_diffuse.png', 'T_Mutant_D.png'), ('Mutant_normal.png', 'T_Mutant_N.png')):
    p = os.path.join(AS, 'Mutant', src)
    if os.path.exists(p): shutil.copyfile(p, os.path.join(out, dst))
report = {'player_scale': kk, 'enemy_scale': km, 'roles': {}}
fps = scene.render.fps / scene.render.fps_base
f0, f1 = (int(round(x)) for x in mut_idle.frame_range)
scene.frame_start, scene.frame_end = f0, f1
mut.animation_data.action = mut_idle
export(os.path.join(fbx_dir, 'E_EnemyIdle.fbx'), [mut], True, km)
report['roles']['EnemyIdle'] = {'source': 'Mutant/Idle.fbx', 'seconds': (f1 - f0) / fps}
print('EXPORTED E_EnemyIdle.fbx')
rename(mut, 'MutantRig')
for role, rel_path in ENEMY_CLIPS.items():
    src, objs, act = import_fbx(os.path.join(A, rel_path))
    for o in objs:
        if o is not src: bpy.data.objects.remove(o, do_unlink=True)
    f0, f1 = (int(round(x)) for x in act.frame_range)
    rename(src, 'Armature')   # same skeleton as the Mutant mesh: export its own action directly
    scene.frame_start, scene.frame_end = f0, f1
    export(os.path.join(fbx_dir, 'E_%s.fbx' % role), [src], True, km)
    report['roles'][role] = {'source': rel_path, 'seconds': (f1 - f0) / fps}
    print('EXPORTED E_%s.fbx' % role)
    bpy.data.objects.remove(src, do_unlink=True)

# ================= player clips: Mixamo -> Epic retarget =================
rename(kra, 'Armature')
kra.data.pose_position = 'POSE'
ensure_anim(kra)
TREST = {b.name: b.matrix_local.copy() for b in kra.data.bones}
order = []
def walk(b):
    order.append(b.name)
    for c in b.children: walk(c)
for b in kra.data.bones:
    if b.parent is None: walk(b)
for pb in kra.pose.bones: pb.rotation_mode = 'QUATERNION'


def retarget(src, act, role):
    srest = {b.name: b.matrix_local.copy() for b in src.data.bones}
    def sname(t): return M + MAP[t] if t in MAP and (M + MAP[t]) in srest else None
    # rest alignment: rotate each mapped target bone so its aim direction matches the source's at rest (A-pose vs T-pose)
    align = {}
    for t in MAP:
        s = sname(t)
        if not s or t not in TREST: continue
        at = AIM.get(t); asb = sname(at) if at else None
        if at and asb and at in TREST:
            dt = (TREST[at].translation - TREST[t].translation).normalized()
            ds = (srest[asb].translation - srest[s].translation).normalized()
            align[t] = dt.rotation_difference(ds)
    hs = srest.get(M + 'Hips')
    ratio = (TREST['pelvis'].translation.z / hs.translation.z) if hs and hs.translation.z > 1e-4 else 1.0
    src.animation_data.action = act
    new = bpy.data.actions.new('A_' + role)
    kra.animation_data.action = new
    f0, f1 = (int(round(x)) for x in act.frame_range)
    prevq = {}
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        pose = {}
        for name in order:
            b = kra.data.bones[name]
            rest = TREST[name]
            parent = b.parent.name if b.parent else None
            rel = (TREST[parent].inverted() @ rest) if parent else rest
            base = (pose[parent] @ rel) if parent else rest.copy()
            s = sname(name)
            if s:
                sp = src.pose.bones[s]
                dq = sp.matrix.to_quaternion() @ srest[s].to_quaternion().inverted()   # source world delta from its rest
                rq = rest.to_quaternion()
                if name in align: rq = align[name] @ rq                                 # target rest aligned to source rest
                rq = (dq @ rq).normalized()
                pos = (rest.translation + (sp.matrix.translation - srest[s].translation) * ratio) if name == 'pelvis' else base.translation
                pose[name] = Matrix.Translation(pos) @ rq.to_matrix().to_4x4()
            else:
                pose[name] = base
            basis = (rel.inverted() @ pose[parent].inverted() @ pose[name]) if parent else (rest.inverted() @ pose[name])
            pb = kra.pose.bones[name]
            q = basis.to_quaternion()
            if name in prevq and prevq[name].dot(q) < 0: q.negate()
            prevq[name] = q
            pb.rotation_quaternion = q
            pb.keyframe_insert('rotation_quaternion', frame=f, group=name)
            if name == 'pelvis':
                pb.location = basis.translation
                pb.keyframe_insert('location', frame=f, group=name)
    return f0, f1, sum(1 for t in MAP if sname(t))


for role, rel_path in PLAYER_CLIPS.items():
    path = os.path.join(A, rel_path)
    if not os.path.exists(path): print('WARN missing', rel_path); continue
    src, objs, act = import_fbx(path)
    for o in objs:
        if o is not src: bpy.data.objects.remove(o, do_unlink=True)
    if not act: print('WARN no action in', rel_path); bpy.data.objects.remove(src, do_unlink=True); continue
    src.name = 'SourceRig'
    f0, f1, matched = retarget(src, act, role)
    scene.frame_start, scene.frame_end = f0, f1
    rename(kra, 'Armature')
    export(os.path.join(fbx_dir, 'A_%s.fbx' % role), [kra], True, kk)
    report['roles'][role] = {'source': rel_path, 'frames': [f0, f1], 'seconds': (f1 - f0) / fps, 'mappedBones': matched}
    print('EXPORTED A_%s.fbx <- %s (%.2fs, %d mapped bones)' % (role, rel_path, (f1 - f0) / fps, matched))
    bpy.data.objects.remove(src, do_unlink=True)

# ================= the Leviathan Axe (static mesh) =================
before = set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=os.path.join(AS, 'Axe', 'source', 'LEVIATHAN Axe_lowAO.fbx'))
new_objs = [o for o in bpy.data.objects if o not in before]
axe_objs = [o for o in new_objs if o.type == 'MESH']
for o in bpy.data.objects: o.select_set(o in axe_objs)
bpy.context.view_layer.objects.active = axe_objs[0]
bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
if len(axe_objs) > 1: bpy.ops.object.join()
axe = bpy.context.view_layer.objects.active; axe.name = 'LeviathanAxe'
for o in new_objs:
    if o.name in bpy.data.objects and o is not axe and o.type != 'MESH': bpy.data.objects.remove(o, do_unlink=True)
vs = [v.co.copy() for v in axe.data.vertices]
lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs))); hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
size = hi - lo
haft = max(range(3), key=lambda i: size[i])
blade_idx = [i for i, sl in enumerate(axe.material_slots) if sl.material and 'Blade' in sl.material.name]
bverts = {vi for p in axe.data.polygons if p.material_index in blade_idx for vi in p.vertices}
ctr = (lo + hi) / 2
bc = (sum((axe.data.vertices[i].co for i in bverts), Vector()) / len(bverts)) if bverts else hi.copy()
hdir = Vector((0.0, 0.0, 0.0)); hdir[haft] = 1.0 if bc[haft] > ctr[haft] else -1.0   # pommel -> head
side = bc - ctr; side[haft] = 0.0
if side.length < 1e-6: side = Vector((1.0, 0.0, 0.0)) if haft != 0 else Vector((0.0, 1.0, 0.0))
side.normalize()
z = hdir; x = (side - z * side.dot(z)).normalized(); y = z.cross(x)
R = Matrix((x, y, z)).to_4x4()                 # rows = new axes: old -> new
length = size[haft]
grip = ctr - hdir * (length / 2) + hdir * (length * 0.18)
axe.data.transform(R @ Matrix.Translation(-grip))
axe.data.transform(Matrix.Scale(0.80 / length, 4))
print('AXE verts=%d length %.3f -> 0.80 m, haft axis %d, materials %s' % (len(axe.data.vertices), length, haft, [sl.material.name for sl in axe.material_slots if sl.material]))
export(os.path.join(fbx_dir, 'LeviathanAxe.fbx'), [axe], False, 1.0, {'MESH'})
print('EXPORTED LeviathanAxe.fbx')
tdir = os.path.join(AS, 'Axe', 'textures')
AXE_TEX = {'T_Axe_Blade_D': 'Axe_Blade_Base_Color.png', 'T_Axe_Blade_N': 'Axe_Blade_Normal_DirectX.png', 'T_Axe_Blade_E': 'LEVIATHAN_Axe_low_Axe_Blade_Emissive.png',
           'T_Axe_Handle_D': 'LEVIATHAN_Axe_low_Axe_Handle_BaseColor.png', 'T_Axe_Handle_N': 'LEVIATHAN_Axe_low_Axe_Handle_Normal.png',
           'T_Axe_Bands_D': 'LEVIATHAN_Axe_low_Bands_BaseColor.png', 'T_Axe_Bands_N': 'LEVIATHAN_Axe_low_Bands_Normal.png',
           'T_Axe_Leather_D': 'LEVIATHAN_Axe_low_Leather_BaseColor.png', 'T_Axe_Leather_N': 'LEVIATHAN_Axe_low_Leather_Normal.png'}
for dst, src in AXE_TEX.items():
    p = os.path.join(tdir, src)
    if not os.path.exists(p): print('WARN missing texture', src); continue
    img = bpy.data.images.load(p)
    if max(img.size) > 2048:
        w, h = img.size; f = 2048 / max(w, h); img.scale(max(1, int(w * f)), max(1, int(h * f)))
    img.filepath_raw = os.path.join(out, dst + '.png'); img.file_format = 'PNG'; img.save()
json.dump(report, open(os.path.join(out, 'clips.json'), 'w'), indent=1)

# ================= previews =================
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'; scene.display.shading.color_type = 'TEXTURE'
scene.render.resolution_x, scene.render.resolution_y = 600, 800
scene.world = bpy.data.worlds.new('w'); scene.world.color = (0.08, 0.08, 0.1)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = 'ORTHO'
for o in mut_meshes + [axe]: o.hide_render = True
kra.data.pose_position = 'REST'; bpy.context.view_layer.update()
lo, hi = bounds(kra_meshes); vctr = (lo + hi) / 2; cam.data.ortho_scale = (hi.z - lo.z) * 1.25


def shoot(name, ang, c):
    d = Vector((0, -1, 0)); d.rotate(Matrix.Rotation(math.radians(ang), 3, 'Z'))
    cam.location = c + d * 10.0
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(out, name)
    bpy.ops.render.render(write_still=True)


shoot('preview_kratos_rest.png', 30, vctr)
kra.data.pose_position = 'POSE'
for role, frac in (('Idle', 0.3), ('WalkF', 0.25), ('WalkR', 0.25), ('RunF', 0.3), ('Attack', 0.3), ('Attack', 0.45), ('Death', 0.9)):
    act = bpy.data.actions.get('A_' + role)
    if not act: continue
    kra.animation_data.action = act
    a0, a1 = act.frame_range
    scene.frame_set(int(a0 + (a1 - a0) * frac))
    shoot('pose_%s_%02d.png' % (role, int(frac * 100)), 30, vctr)
for o in kra_meshes: o.hide_render = True
axe.hide_render = False
alo, ahi = bounds([axe]); cam.data.ortho_scale = max(ahi - alo) * 1.3
shoot('preview_axe.png', 0, (alo + ahi) / 2)
print('DONE')
