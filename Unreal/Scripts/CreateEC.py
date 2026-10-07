"""Minecraft x Elden Combat assets (UE4 Python commandlet). PRIVATE BUILD.
Imports the user's custom 3D Steve rig (the same Blender build as the Physics & Portal mod: Work\\pp\\build, made by
Avatar/blender/pp_steve_build.py) into its own folder /Game/Crb/EC, so the two mods never share or overwrite assets:
SK_ECSteve (+ skeleton, physics asset), the E_* combat clips, T_ECSteve_Skin and M_ECSteve.
Runtime: FCrbECSteve / CrbEldenCombat.cpp (C++)."""
import os
import unreal

# reuse the import helpers of CreatePP.py (task / fbx_ui / pixel_texture / material) without running its main()
_dir = os.environ.get('CRB_SCRIPT_DIR') or os.path.dirname(os.path.abspath(globals().get('__file__', 'CreateEC.py')))
_src = open(os.path.join(_dir, 'CreatePP.py')).read()
_src = _src.replace('\nmain()\n', '\n')
exec(compile(_src, 'CreatePP.py', 'exec'))

FOLDER = '/Game/Crb/EC'
CLIPS = ['Idle', 'Walk', 'Run', 'Jump', 'Fall', 'Light1', 'Light2', 'Light3', 'Heavy', 'Charge', 'Thrust', 'Dodge', 'Backstep',
         'Block', 'BlockHit', 'Parry', 'GuardBreak', 'Stagger', 'Riposte', 'Death']   # Source/CrbECSteve.h ECrbECClip


def log(m):
    unreal.log('CRB EC ' + m); print('CRB EC ' + m)


def trail_material():
    """M_ECTrail: unlit translucent ribbon for the Elden Ring style swing trail (vertex colour = colour + alpha)."""
    name = 'M_ECTrail'; path = FOLDER + '/' + name
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    m = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT); m.set_editor_property('two_sided', True)
    vc = MEL.create_material_expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    k = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 200)
    k.set_editor_property('parameter_name', 'Glow'); k.set_editor_property('default_value', 3.0)
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 0)
    MEL.connect_material_expressions(vc, '', mul, 'A'); MEL.connect_material_expressions(k, '', mul, 'B')
    MEL.connect_material_property(mul, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(vc, 'A', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log('material M_ECTrail')


def ec_main():
    fbx = os.path.join(BUILD, 'fbx')
    if not BUILD or not os.path.exists(os.path.join(fbx, 'Steve3D.fbx')):
        log('ERROR no Steve3D.fbx in %s' % fbx); return
    if EAL.does_directory_exist(FOLDER): EAL.delete_directory(FOLDER)
    EAL.make_directory(FOLDER)
    skin = pixel_texture(os.path.join(BUILD, 'T_Steve3D_Skin.png'), 'T_ECSteve_Skin')
    task(os.path.join(fbx, 'Steve3D.fbx'), 'SK_ECSteve', fbx_ui(True, True, False))
    sk = unreal.load_asset(FOLDER + '/SK_ECSteve')
    if not isinstance(sk, unreal.SkeletalMesh): log('ERROR SK_ECSteve import failed'); return
    skeleton = sk.get_editor_property('skeleton')
    m = material('M_ECSteve', skin, 'Skin', masked=True)
    mats = sk.get_editor_property('materials')
    for i in range(len(mats)): mats[i].set_editor_property('material_interface', m)
    sk.set_editor_property('materials', mats); EAL.save_loaded_asset(sk)
    log('mesh SK_ECSteve bounds %s' % sk.get_imported_bounds().box_extent)
    ok = 0
    for c in [p + n for p in ('E_', 'R_') for n in CLIPS]:   # Minecraft style (E_) and the Elden Ring rewrite (R_)
        f = os.path.join(fbx, '%s.fbx' % c)
        if not os.path.exists(f): log('WARN missing clip %s' % c); continue
        want = FOLDER + '/' + c
        for p in task(f, c, fbx_ui(False, True, True, skeleton)):
            a = unreal.load_asset(p)
            if isinstance(a, unreal.AnimSequence) and p.split('.')[0] != want:
                if EAL.does_asset_exist(want): EAL.delete_asset(want)
                EAL.rename_asset(p.split('.')[0], want)
        if isinstance(unreal.load_asset(want), unreal.AnimSequence): ok += 1
    trail_material()
    EAL.save_directory(FOLDER)
    log('DONE clips=%d/%d' % (ok, 2 * len(CLIPS)))


ec_main()
