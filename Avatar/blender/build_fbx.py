"""CRB-AVATAR-01 step A3 (Blender 3.6, headless):
  blender -b --factory-startup -P build_fbx.py -- <build_dir> <ref_dir> [target_height_m]

Reopens build_dir/avatar_rig.blend (skin_build.py), applies the painted textures (T_Avatar_D.png / T_Avatar_N.png from
Avatar/gen/paint_avatar.py), and exports for Unreal:
  build_dir/fbx/Avatar.fbx            rigged mesh (Mixamo skeleton in the A-pose rest), scaled to target height
  build_dir/fbx/A_<Role>.fbx          one clip per animation role, from the reference repo's Mixamo FBX files
  build_dir/clips.json                role -> source file, length, frames (also lists fallbacks)
  build_dir/textured_*.png            textured workbench previews
Clips are exported from their own (T-pose rest) armature: FBX stores parent-relative bone transforms, so on the shared
skeleton in Unreal they drive the A-pose-bound mesh correctly. Both mesh and clips use the same global scale.
Only animation data is taken from the reference repo; its character meshes are discarded."""
import bpy, json, math, os, re, sys, glob

argv = sys.argv[sys.argv.index('--') + 1:]
build, ref = argv[0], argv[1]
target = float(argv[2]) if len(argv) > 2 else 1.80
fbx_dir = os.path.join(build, 'fbx'); os.makedirs(fbx_dir, exist_ok=True)
P = 'mixamorig:'

ROLES = [  # role, patterns (first match wins, case-insensitive on the file name), fallback role
    ('Idle', [r'idle'], None),
    ('Walk', [r'walk'], 'Idle'),
    ('Run', [r'run|sprint|jog'], 'Walk'),
    ('Jump', [r'jump'], 'Idle'),
    ('Fall', [r'fall|falling|air'], 'Jump'),
    ('Land', [r'land'], 'Idle'),
    ('Attack1', [r'(attack|slash|combo|swing|light).*1|light'], None),
    ('Attack2', [r'(attack|slash|combo|swing).*2'], 'Attack1'),
    ('Attack3', [r'(attack|slash|combo|swing|heavy).*3|heavy'], 'Attack2'),
    ('Hit', [r'hit|impact|react|damage|stagger'], 'Idle'),
    ('Fly', [r'fly|float|hover|swim'], 'Fall'),
    ('Death', [r'death|die|dying'], 'Idle'),
    ('Aim', [r'aim'], 'Idle'),
    ('Throw', [r'throw'], 'Attack1'),
    ('Catch', [r'catch|recall|call'], 'Idle'),
]

files = sorted(set(glob.glob(os.path.join(ref, '**', '*.fbx'), recursive=True) + glob.glob(os.path.join(ref, '**', '*.FBX'), recursive=True)))
print('CLIPS found %d fbx files' % len(files))
generic_attacks = [f for f in files if re.search(r'attack|slash|combo|swing|punch|melee', os.path.basename(f), re.I)]

def pick(patterns, used):
    for pat in patterns:
        for f in files:
            if f in used: continue
            if re.search(pat, os.path.basename(f), re.I): return f
    return None

chosen, used = {}, set()
for role, pats, _ in ROLES:
    f = pick(pats, used)
    if f is None and role.startswith('Attack'):
        rest = [g for g in generic_attacks if g not in used]
        f = rest[0] if rest else None
    if f: chosen[role] = f; used.add(f)
fallbacks = {}
for role, _, fb in ROLES:
    if role not in chosen:
        r = fb
        while r and r not in chosen: r = next((x[2] for x in ROLES if x[0] == r), None)
        if r: chosen[role] = chosen[r]; fallbacks[role] = r
print('CLIPS mapping', json.dumps({k: os.path.basename(v) for k, v in chosen.items()}), 'fallbacks', json.dumps(fallbacks))

# ---------------- mesh
bpy.ops.wm.open_mainfile(filepath=os.path.join(build, 'avatar_rig.blend'))
arm = bpy.data.objects['Armature']; body = bpy.data.objects['AvatarBody']
height = max((body.matrix_world @ v.co).z for v in body.data.vertices) - min((body.matrix_world @ v.co).z for v in body.data.vertices)
k = target / height
print('MESH height %.4f m -> scale %.4f' % (height, k))
mat = bpy.data.materials.new('M_CrbAvatar'); mat.use_nodes = True
nt = mat.node_tree; bsdf = nt.nodes.get('Principled BSDF')
diff = os.path.join(build, 'T_Avatar_D.png'); nrm = os.path.join(build, 'T_Avatar_N.png')
if os.path.exists(diff):
    tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = bpy.data.images.load(diff)
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
else:
    print('WARN no T_Avatar_D.png; exporting untextured')
if os.path.exists(nrm):
    nt2 = nt.nodes.new('ShaderNodeTexImage'); nt2.image = bpy.data.images.load(nrm); nt2.image.colorspace_settings.name = 'Non-Color'
    nm = nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(nt2.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
bsdf.inputs['Roughness'].default_value = 0.75
body.data.materials.clear(); body.data.materials.append(mat)

def export(path, objs, anim):
    for o in bpy.context.scene.objects: o.select_set(o in objs)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'} if not anim else {'ARMATURE'},
        global_scale=k, apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL', axis_forward='-Z', axis_up='Y',
        add_leaf_bones=False, use_armature_deform_only=False, primary_bone_axis='Y', secondary_bone_axis='X',
        mesh_smooth_type='FACE', use_mesh_modifiers=True, bake_anim=anim, bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, path_mode='STRIP')

export(os.path.join(fbx_dir, 'Avatar.fbx'), [arm, body], False)
print('EXPORTED Avatar.fbx bones=%d verts=%d' % (len(arm.data.bones), len(body.data.vertices)))

# textured previews
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'; scene.display.shading.color_type = 'TEXTURE'
scene.render.resolution_x, scene.render.resolution_y = 700, 1000
import mathutils
scene.world = bpy.data.worlds.new('w'); scene.world.color = (0.05, 0.05, 0.05)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = 'ORTHO'; cam.data.ortho_scale = height * 1.1
ws = [body.matrix_world @ v.co for v in body.data.vertices]
ctr = mathutils.Vector(((max(w.x for w in ws) + min(w.x for w in ws)) / 2, (max(w.y for w in ws) + min(w.y for w in ws)) / 2, (max(w.z for w in ws) + min(w.z for w in ws)) / 2))
for name, ang in (('front', 0), ('three_quarter', 45), ('back', 180)):
    d = mathutils.Vector((0, -1, 0)); d.rotate(mathutils.Euler((0, 0, math.radians(ang))))
    cam.location = ctr + d * 6.0; cam.rotation_euler = (-d).to_track_quat('-Z', 'Z').to_euler()
    scene.render.filepath = os.path.join(build, 'textured_%s.png' % name)
    bpy.ops.render.render(write_still=True)

# ---------------- clips
out = {'scale': k, 'source_height_m': height, 'roles': {}, 'fallbacks': fallbacks}
for role, _, _ in ROLES:
    src = chosen.get(role)
    if not src: print('WARN no clip for', role); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=src, automatic_bone_orientation=False, ignore_leaf_bones=False)
    carm = next((o for o in bpy.context.scene.objects if o.type == 'ARMATURE'), None)
    if not carm or not carm.animation_data or not carm.animation_data.action:
        print('WARN %s: %s has no armature action' % (role, os.path.basename(src))); continue
    for o in list(bpy.context.scene.objects):
        if o.type != 'ARMATURE': bpy.data.objects.remove(o, do_unlink=True)
    carm.name = 'Armature'; carm.data.name = 'Armature'
    # Bake the import transform the same way skin_build.py did; pose-bone location keys are bone-local, so they need
    # the object's (uniform) scale applied by hand.
    s_obj = carm.scale.x
    bpy.context.view_layer.objects.active = carm; carm.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    act = carm.animation_data.action
    for fc in act.fcurves:
        if fc.data_path.endswith('.location'):
            for kp in fc.keyframe_points:
                kp.co[1] *= s_obj; kp.handle_left[1] *= s_obj; kp.handle_right[1] *= s_obj
    f0, f1 = act.frame_range
    scene = bpy.context.scene; scene.frame_start, scene.frame_end = int(f0), int(f1)
    fps = scene.render.fps / scene.render.fps_base
    missing = [b.name for b in carm.data.bones if not b.name.startswith(P)]
    path = os.path.join(fbx_dir, 'A_%s.fbx' % role)
    export(path, [carm], True)
    out['roles'][role] = {'source': os.path.relpath(src, ref), 'frames': [f0, f1], 'seconds': (f1 - f0) / fps, 'bones': len(carm.data.bones), 'nonMixamoBones': missing[:8]}
    print('EXPORTED A_%s.fbx <- %s (%.2fs)' % (role, os.path.basename(src), (f1 - f0) / fps))
json.dump(out, open(os.path.join(build, 'clips.json'), 'w'), indent=1)
print('DONE')
