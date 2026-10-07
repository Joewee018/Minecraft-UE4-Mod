"""SM64 Steve Movement - offline UE4.27 editor step (UE4Editor-Cmd -run=pythonscript), run by Tools/BuildUnreal.ps1
when Work/steve/build/fbx/Steve.fbx exists. Environment: CRB_STEVE_BUILD = that build directory.

Imports into /Game/Crb/Steve:
  SK_Steve (+ skeleton)   fbx/Steve.fbx           the Minecraft player boxes on an original rig
  S_<Clip>                fbx/S_<Clip>.fbx        original keyframed clips (Avatar/blender/steve_build.py)
  T_SteveSkin_Default     placeholder skin (the live Minecraft skin replaces it at runtime via the 'Skin' parameter)
  M_Steve_Base / M_Steve_Overlay
No Blueprint: the runtime anim instance is the native UCrbSteveAnimInstance."""
import os
import unreal

BUILD = os.environ.get('CRB_STEVE_BUILD', '')
FOLDER = '/Game/Crb/Steve'
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
# Source/CrbSteveAnim.h ECrbSteveClip order
CLIPS = ['Idle', 'Walk', 'Run', 'Skid', 'Brake', 'Crouch', 'CrouchSlide', 'Jump', 'DoubleJump', 'TripleJump', 'Backflip',
         'Sideflip', 'LongJump', 'Fall', 'Land', 'HardLand', 'GroundPoundSpin', 'GroundPoundFall', 'GroundPoundLand',
         'WallCling', 'WallKick', 'Bonk']


def log(msg):
    unreal.log('CRB STEVE ' + msg)
    print('CRB STEVE ' + msg)


def task(filename, name, options):
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', filename)
    t.set_editor_property('destination_path', FOLDER)
    t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', True)
    if options is not None:
        t.set_editor_property('options', options)
    tools.import_asset_tasks([t])
    return list(t.get_editor_property('imported_object_paths') or [])


def import_skeletal(fbx, name):
    o = unreal.FbxImportUI()
    o.set_editor_property('import_mesh', True)
    o.set_editor_property('import_as_skeletal', True)
    o.set_editor_property('import_animations', False)
    o.set_editor_property('import_materials', False)
    o.set_editor_property('import_textures', False)
    o.set_editor_property('create_physics_asset', False)
    o.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    d = o.get_editor_property('skeletal_mesh_import_data')
    d.set_editor_property('import_morph_targets', False)
    d.set_editor_property('use_t0_as_ref_pose', False)
    d.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task(fbx, name, o)
    return unreal.load_asset(FOLDER + '/' + name)


def import_anim(fbx, name, skeleton):
    o = unreal.FbxImportUI()
    o.set_editor_property('import_mesh', False)
    o.set_editor_property('import_as_skeletal', True)
    o.set_editor_property('import_animations', True)
    o.set_editor_property('import_materials', False)
    o.set_editor_property('import_textures', False)
    o.set_editor_property('skeleton', skeleton)
    o.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_ANIMATION)
    a = o.get_editor_property('anim_sequence_import_data')
    a.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    a.set_editor_property('import_bone_tracks', True)
    paths = task(fbx, name, o)
    want = FOLDER + '/' + name
    for p in paths:
        asset = unreal.load_asset(p)
        if isinstance(asset, unreal.AnimSequence) and p.split('.')[0] != want:
            if EAL.does_asset_exist(want):
                EAL.delete_asset(want)
            EAL.rename_asset(p.split('.')[0], want)
    return unreal.load_asset(want)


def ck(ok, what):
    if not ok:
        log('ERROR material connection failed: ' + what)
    return ok


def material(name, skin, masked):
    """Skin texture parameter 'Skin' (point-sampled 64x64), world-matched shading like the Avatar materials:
    lit base x 'LitWeight' plus a vanilla-style emissive term x 'Brightness' (Minecraft light level at the eye)."""
    path = FOLDER + '/' + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    t = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    t.set_editor_property('parameter_name', 'Skin')
    t.set_editor_property('texture', skin)
    lw = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, -180)
    lw.set_editor_property('parameter_name', 'LitWeight'); lw.set_editor_property('default_value', 1.0)
    bm = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, -100)
    ck(MEL.connect_material_expressions(t, 'RGB', bm, 'A'), 'base A'); ck(MEL.connect_material_expressions(lw, '', bm, 'B'), 'base B')
    ck(MEL.connect_material_property(bm, '', unreal.MaterialProperty.MP_BASE_COLOR), 'base color')
    b = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 300)
    b.set_editor_property('parameter_name', 'Brightness'); b.set_editor_property('default_value', 0.8)
    em = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 300)
    ck(MEL.connect_material_expressions(t, 'RGB', em, 'A'), 'emissive A'); ck(MEL.connect_material_expressions(b, '', em, 'B'), 'emissive B')
    ck(MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'emissive')
    r = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -250, 500)
    r.set_editor_property('r', 0.85)
    ck(MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS), 'roughness')
    if masked:
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property('two_sided', True)
        m.set_editor_property('opacity_mask_clip_value', 0.5)
        ck(MEL.connect_material_property(t, 'A', unreal.MaterialProperty.MP_OPACITY_MASK), 'opacity mask')
    m.set_editor_property('used_with_skeletal_mesh', True)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('material %s masked=%s' % (name, masked))
    return m


def main():
    fbx_dir = os.path.join(BUILD, 'fbx')
    if not BUILD or not os.path.exists(os.path.join(fbx_dir, 'Steve.fbx')):
        log('ERROR no Steve.fbx in %s' % fbx_dir)
        return
    if EAL.does_directory_exist(FOLDER):
        EAL.delete_directory(FOLDER)
    EAL.make_directory(FOLDER)
    skin = None
    png = os.path.join(BUILD, 'T_SteveSkin_Default.png')
    if os.path.exists(png):
        task(png, 'T_SteveSkin_Default', None)
        skin = unreal.load_asset(FOLDER + '/T_SteveSkin_Default')
        if skin:
            skin.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
            skin.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            skin.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
            # Pixel art must not be block-compressed: DXT averaged each 4x4 block (lost eye pupils, split face shading).
            skin.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
            skin.set_editor_property('srgb', True)
            skin.set_editor_property('never_stream', True)
            log('skin texture %s uncompressed (TC_EditorIcon), nearest, no mips' % skin.get_name())
            EAL.save_loaded_asset(skin)
    sk = import_skeletal(os.path.join(fbx_dir, 'Steve.fbx'), 'SK_Steve')
    if not isinstance(sk, unreal.SkeletalMesh):
        log('ERROR SK_Steve import failed')
        return
    skeleton = sk.get_editor_property('skeleton')
    log('mesh %s skeleton %s' % (sk.get_path_name(), skeleton.get_path_name() if skeleton else None))
    base = material('M_Steve_Base', skin, False)
    over = material('M_Steve_Overlay', skin, True)
    mats = sk.get_editor_property('materials')
    for i in range(len(mats)):
        slot = str(mats[i].get_editor_property('material_slot_name'))
        mats[i].set_editor_property('material_interface', over if 'overlay' in slot.lower() else base)
        log('slot %d "%s" -> %s' % (i, slot, 'overlay' if 'overlay' in slot.lower() else 'base'))
    sk.set_editor_property('materials', mats)
    EAL.save_loaded_asset(sk)
    try:
        log('bounds extent %s' % sk.get_imported_bounds().box_extent)
    except Exception as ex:
        log('bounds unavailable: %s' % ex)
    ok = 0
    for c in CLIPS:
        f = os.path.join(fbx_dir, 'S_%s.fbx' % c)
        if not os.path.exists(f):
            log('WARN missing clip S_%s' % c)
            continue
        a = import_anim(f, 'S_' + c, skeleton)
        if isinstance(a, unreal.AnimSequence):
            ok += 1
            log('clip S_%s %.2fs' % (c, a.get_editor_property('sequence_length')))
        else:
            log('WARN clip S_%s import failed' % c)
    EAL.save_directory(FOLDER)
    log('DONE clips=%d/%d' % (ok, len(CLIPS)))


main()
