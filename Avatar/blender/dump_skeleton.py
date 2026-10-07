"""blender -b --factory-startup -P dump_skeleton.py -- <clip.fbx> <out.json>
Imports one Mixamo FBX and writes its armature rest pose (armature space, metres) + animation info as JSON."""
import bpy, json, sys
argv = sys.argv[sys.argv.index('--') + 1:]
src, out = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src, automatic_bone_orientation=False, ignore_leaf_bones=False)
arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
mw = arm.matrix_world
bones = []
for b in arm.data.bones:
    bones.append({
        'name': b.name, 'parent': b.parent.name if b.parent else None,
        'head': list(mw @ b.head_local), 'tail': list(mw @ b.tail_local),
        'matrix': [list(r) for r in (mw @ b.matrix_local)],
    })
meshes = [{'name': o.name, 'verts': len(o.data.vertices)} for o in bpy.context.scene.objects if o.type == 'MESH']
act = arm.animation_data.action if arm.animation_data and arm.animation_data.action else None
info = {'source': src, 'armature': arm.name, 'armatureScale': list(arm.scale), 'matrixWorld': [list(r) for r in mw],
        'bones': bones, 'meshes': meshes,
        'action': None if not act else {'name': act.name, 'frames': list(act.frame_range), 'fcurves': len(act.fcurves)},
        'fps': bpy.context.scene.render.fps, 'unitScale': bpy.context.scene.unit_settings.scale_length}
json.dump(info, open(out, 'w'), indent=1)
print('DUMPED', len(bones), 'bones ->', out)
