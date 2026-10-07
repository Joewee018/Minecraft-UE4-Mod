"""God of War Unity port - offline UE4.27 editor step (UE4Editor-Cmd -run=pythonscript), run by Tools/BuildUnreal.ps1
when Work/avatar/build/fbx/Avatar.fbx exists. Environment: CRB_AVATAR_BUILD = that build directory.
PRIVATE BUILD: imports the repo's Kratos and Leviathan Axe models (not licensed by the repo).

Imports into /Game/Crb/Avatar:
  SK_Avatar  (+ skeleton)  fbx/Avatar.fbx        Kratos, the player       clips A_<Role>
  SK_Mutant  (+ skeleton)  fbx/Mutant.fbx        the Mixamo Mutant enemy  clips E_<Role>
  SM_LeviathanAxe          fbx/LeviathanAxe.fbx  the axe (static mesh)
  textures T_* and one material per mesh slot
No Blueprint: the runtime anim instance is the native UCrbAvatarAnimInstance."""
import os
import unreal

BUILD = os.environ.get('CRB_AVATAR_BUILD', '')
FOLDER = '/Game/Crb/Avatar'
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
PLAYER_ROLES = ['Idle', 'WalkF', 'WalkB', 'WalkL', 'WalkR', 'RunF', 'RunB', 'JogL', 'JogR', 'Attack', 'Death']  # Source/CrbAvatarAnim.h
ENEMY_ROLES = ['EnemyIdle', 'Swipe', 'EnemyDeath']
# slot-name keyword -> (diffuse, normal, emissive, masked)
SLOT_TEX = [
    # FaceAcc is the face geometry; the repo's Unity material put the fur texture (beard_d) on it, which renders the
    # face black. Its UVs sit in head_d's face region (face + painted beard), so it uses the head texture.
    ('FaceAcc', ('T_Kratos_Head_D', None, None, False)),
    ('Head', ('T_Kratos_Head_D', None, None, False)),
    ('Body', ('T_Kratos_Body_D', None, None, False)),
    ('Blade', ('T_Axe_Blade_D', 'T_Axe_Blade_N', 'T_Axe_Blade_E', False)),
    ('Handle', ('T_Axe_Handle_D', 'T_Axe_Handle_N', None, False)),
    ('Bands', ('T_Axe_Bands_D', 'T_Axe_Bands_N', None, False)),
    ('Leather', ('T_Axe_Leather_D', 'T_Axe_Leather_N', None, False)),
    ('Mutant', ('T_Mutant_D', 'T_Mutant_N', None, False)),
]


def log(msg):
    unreal.log('CRB AVATAR ' + msg)
    print('CRB AVATAR ' + msg)


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


def import_static(fbx, name):
    o = unreal.FbxImportUI()
    o.set_editor_property('import_mesh', True)
    o.set_editor_property('import_as_skeletal', False)
    o.set_editor_property('import_animations', False)
    o.set_editor_property('import_materials', False)
    o.set_editor_property('import_textures', False)
    o.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    d = o.get_editor_property('static_mesh_import_data')
    d.set_editor_property('combine_meshes', True)
    d.set_editor_property('auto_generate_collision', False)
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


_tex_cache = {}


def texture(name, normal=False):
    if name in _tex_cache:
        return _tex_cache[name]
    png = os.path.join(BUILD, name + '.png')
    if not os.path.exists(png):
        _tex_cache[name] = None
        return None
    task(png, name, None)
    t = unreal.load_asset(FOLDER + '/' + name)
    if t and normal:
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property('srgb', False)
        EAL.save_loaded_asset(t)
    _tex_cache[name] = t
    return t


def ck(ok, what):
    if not ok:
        log('ERROR material connection failed: ' + what)
    return ok


def material(name, diff, nrm, emi, masked):
    path = FOLDER + '/' + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    emissive_inputs = []
    if diff:
        t = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSample, -400, 0)
        t.set_editor_property('texture', diff)
        # Lit part weighted like the world's sun term ('LitWeight' = host SunWeight)
        lw = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, -150)
        lw.set_editor_property('parameter_name', 'LitWeight'); lw.set_editor_property('default_value', 1.0)
        bm = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, -100)
        ck(MEL.connect_material_expressions(t, 'RGB', bm, 'A'), 'base x litweight A'); ck(MEL.connect_material_expressions(lw, '', bm, 'B'), 'base x litweight B')
        ck(MEL.connect_material_property(bm, '', unreal.MaterialProperty.MP_BASE_COLOR), 'base color')
        # Like the world's blocks: a vanilla-style emissive term (texture x Minecraft light level, set at runtime via
        # the 'Brightness' parameter) on top of the lit result, so the tonemapped scene shows the textures at full value.
        b = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 380)
        b.set_editor_property('parameter_name', 'Brightness'); b.set_editor_property('default_value', 0.8)
        mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 380)
        ck(MEL.connect_material_expressions(t, 'RGB', mul, 'A'), "t, 'RGB', mul, 'A'"); ck(MEL.connect_material_expressions(b, '', mul, 'B'), "b, '', mul, 'B'")
        emissive_inputs.append(mul)
        if masked:
            m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
            m.set_editor_property('two_sided', True)
            ck(MEL.connect_material_property(t, 'A', unreal.MaterialProperty.MP_OPACITY_MASK), "t, 'A', unreal.MaterialProperty.MP_OPACITY_MASK")
    if nrm:
        n = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSample, -400, 250)
        n.set_editor_property('texture', nrm)
        n.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        ck(MEL.connect_material_property(n, 'RGB', unreal.MaterialProperty.MP_NORMAL), "n, 'RGB', unreal.MaterialProperty.MP_NORMAL")
    if emi:
        e = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSample, -400, 500)
        e.set_editor_property('texture', emi)
        emissive_inputs.append(e)
    if len(emissive_inputs) == 1:
        ck(MEL.connect_material_property(emissive_inputs[0], '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), "emissive_inputs[0], '', unreal.MaterialProperty.MP_EMISSIVE_COLOR")
    elif len(emissive_inputs) == 2:
        add = MEL.create_material_expression(m, unreal.MaterialExpressionAdd, -100, 450)
        ck(MEL.connect_material_expressions(emissive_inputs[0], '', add, 'A'), "emissive_inputs[0], '', add, 'A'"); ck(MEL.connect_material_expressions(emissive_inputs[1], 'RGB', add, 'B'), "emissive_inputs[1], 'RGB', add, 'B'")
        ck(MEL.connect_material_property(add, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), "add, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR")
    if masked == 'hide':
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        z = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -400, 0)
        z.set_editor_property('r', 0.0)
        ck(MEL.connect_material_property(z, '', unreal.MaterialProperty.MP_OPACITY_MASK), 'hidden opacity')
    r = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -200, 650)
    r.set_editor_property('r', 0.7)
    ck(MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS), "r, '', unreal.MaterialProperty.MP_ROUGHNESS")
    m.set_editor_property('used_with_skeletal_mesh', True)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('material %s base=%s normal=%s emissive=%s masked=%s' % (name, diff.get_name() if diff else None, nrm.get_name() if nrm else None, emi.get_name() if emi else None, masked))
    return m


def tex_for(slot_name):
    for key, spec in SLOT_TEX:
        if key.lower() in slot_name.lower():
            return spec
    return None


def apply_materials(mesh, default_key):
    prop = 'materials' if isinstance(mesh, unreal.SkeletalMesh) else 'static_materials'
    mats = mesh.get_editor_property(prop)
    for i in range(len(mats)):
        slot = str(mats[i].get_editor_property('material_slot_name'))
        d, n, e, masked = tex_for(slot) or dict(SLOT_TEX)[default_key]
        m = material('M_%s_%d' % (mesh.get_name(), i), texture(d) if d else None, texture(n, True) if n else None, texture(e) if e else None, masked)
        mats[i].set_editor_property('material_interface', m)
        log('%s slot %d "%s" -> %s' % (mesh.get_name(), i, slot, d))
    mesh.set_editor_property(prop, mats)
    EAL.save_loaded_asset(mesh)


def main():
    fbx_dir = os.path.join(BUILD, 'fbx')
    if not BUILD or not os.path.exists(os.path.join(fbx_dir, 'Avatar.fbx')):
        log('ERROR no Avatar.fbx in %s' % fbx_dir)
        return
    if EAL.does_directory_exist(FOLDER):
        EAL.delete_directory(FOLDER)   # clean rebuild: skeletons must not merge with an earlier import
    EAL.make_directory(FOLDER)
    ok = 0
    for mesh_name, fbx, roles, prefix, default_key in (('SK_Avatar', 'Avatar.fbx', PLAYER_ROLES, 'A_', 'Body'), ('SK_Mutant', 'Mutant.fbx', ENEMY_ROLES, 'E_', 'Mutant')):
        sk = import_skeletal(os.path.join(fbx_dir, fbx), mesh_name)
        if not isinstance(sk, unreal.SkeletalMesh):
            log('ERROR %s import failed' % mesh_name)
            continue
        skeleton = sk.get_editor_property('skeleton')
        log('mesh %s skeleton %s' % (sk.get_path_name(), skeleton.get_path_name() if skeleton else None))
        apply_materials(sk, default_key)
        try:
            log('%s bounds extent %s' % (mesh_name, sk.get_imported_bounds().box_extent))
        except Exception as ex:
            log('bounds unavailable: %s' % ex)
        for role in roles:
            f = os.path.join(fbx_dir, '%s%s.fbx' % (prefix, role))
            if not os.path.exists(f):
                log('WARN missing clip %s%s' % (prefix, role))
                continue
            a = import_anim(f, prefix + role, skeleton)
            if isinstance(a, unreal.AnimSequence):
                ok += 1
                log('clip %s%s %.2fs' % (prefix, role, a.get_editor_property('sequence_length')))
            else:
                log('WARN clip %s%s import failed' % (prefix, role))
    axe_fbx = os.path.join(fbx_dir, 'LeviathanAxe.fbx')
    if os.path.exists(axe_fbx):
        sm = import_static(axe_fbx, 'SM_LeviathanAxe')
        if isinstance(sm, unreal.StaticMesh):
            apply_materials(sm, 'Blade')
            log('axe SM_LeviathanAxe imported')
    EAL.save_directory(FOLDER)
    log('DONE clips=%d/%d' % (ok, len(PLAYER_ROLES) + len(ENEMY_ROLES)))


main()
