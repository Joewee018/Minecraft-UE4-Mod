"""Physics & Portal mod - offline UE4.27 editor step, run by Tools/BuildUnreal.ps1 when Work/pp/build/fbx/Steve3D.fbx
exists. Environment: CRB_PP_BUILD = that directory. PRIVATE BUILD (the user's sculpted 3D Steve).
Imports into /Game/Crb/PP: SK_PPSteve (+ skeleton + generated physics asset for ragdoll), P_<Clip> clips,
T_PPSteve_Skin (uncompressed pixel art), M_PPSteve, and M_Portal (portal surface: live view render target sampled in
screen space, oval mask, glowing rim). Runtime: FCrbPPSteve / CrbPhysicsPortal.cpp (C++)."""
import os, json
import unreal

BUILD = os.environ.get('CRB_PP_BUILD', '')
FOLDER = '/Game/Crb/PP'
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
CLIPS = ['Idle', 'Walk', 'Run', 'Jump', 'Fall', 'Land', 'Roll', 'Slide', 'Launch', 'PortalEnter', 'PortalExit', 'HitReact']   # Source/CrbPPSteve.h ECrbPPClip


def log(m):
    unreal.log('CRB PP ' + m); print('CRB PP ' + m)


def task(filename, name, options):
    t = unreal.AssetImportTask()
    for k, v in (('filename', filename), ('destination_path', FOLDER), ('destination_name', name), ('automated', True), ('replace_existing', True), ('save', True)):
        t.set_editor_property(k, v)
    if options is not None: t.set_editor_property('options', options)
    tools.import_asset_tasks([t])
    return list(t.get_editor_property('imported_object_paths') or [])


def fbx_ui(mesh, skeletal, anim, skeleton=None):
    o = unreal.FbxImportUI()
    o.set_editor_property('import_mesh', mesh); o.set_editor_property('import_as_skeletal', skeletal)
    o.set_editor_property('import_animations', anim); o.set_editor_property('import_materials', False); o.set_editor_property('import_textures', False)
    o.set_editor_property('create_physics_asset', bool(mesh and skeletal))   # ragdoll bodies for SK_PPSteve
    if skeleton: o.set_editor_property('skeleton', skeleton)
    o.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_ANIMATION if anim and not mesh else (unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH))
    if skeletal and mesh:
        d = o.get_editor_property('skeletal_mesh_import_data')
        d.set_editor_property('import_morph_targets', False); d.set_editor_property('use_t0_as_ref_pose', False)
        d.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS)
    if not skeletal:
        d = o.get_editor_property('static_mesh_import_data')
        d.set_editor_property('combine_meshes', True); d.set_editor_property('auto_generate_collision', False)
    if anim:
        a = o.get_editor_property('anim_sequence_import_data')
        a.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        a.set_editor_property('import_bone_tracks', True)
    return o


def pixel_texture(png, name):
    task(png, name, None)
    t = unreal.load_asset(FOLDER + '/' + name)
    if t:
        t.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
        t.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        t.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
        t.set_editor_property('never_stream', True)
        EAL.save_loaded_asset(t)
    return t


def material(name, tex, param, masked=False, normal=None, rough=None):
    path = FOLDER + '/' + name
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    m = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    if tex:
        t = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
        t.set_editor_property('parameter_name', param)
        t.set_editor_property('texture', tex)
    else:   # untextured slot (e.g. a board part with only a material colour): mid grey, never an empty sampler
        t = MEL.create_material_expression(m, unreal.MaterialExpressionConstant4Vector, -500, 0)
        t.set_editor_property('constant', unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
        masked = False
    pin = 'RGB' if tex else ''
    lw = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, -180)
    lw.set_editor_property('parameter_name', 'LitWeight'); lw.set_editor_property('default_value', 1.0)
    bm = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, -100)
    MEL.connect_material_expressions(t, pin, bm, 'A'); MEL.connect_material_expressions(lw, '', bm, 'B')
    MEL.connect_material_property(bm, '', unreal.MaterialProperty.MP_BASE_COLOR)
    b = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 300)
    b.set_editor_property('parameter_name', 'Brightness'); b.set_editor_property('default_value', 0.8)
    em = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 300)
    MEL.connect_material_expressions(t, pin, em, 'A'); MEL.connect_material_expressions(b, '', em, 'B')
    MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if rough:
        r = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSample, -500, 500); r.set_editor_property('texture', rough)
        r.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        MEL.connect_material_property(r, 'R', unreal.MaterialProperty.MP_ROUGHNESS)
    else:
        r = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -250, 500); r.set_editor_property('r', 0.8)
        MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    if normal:
        n = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSample, -500, 700); n.set_editor_property('texture', normal)
        n.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_property(n, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    if masked:
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED); m.set_editor_property('two_sided', True)
        MEL.connect_material_property(t, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    m.set_editor_property('used_with_skeletal_mesh', True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    log('material %s (%s)' % (name, tex.get_name() if tex else 'no texture'))
    return m



def portal_material():
    """M_Portal: unlit, masked oval. Emissive = the linked portal's live view (render target 'View', sampled with the
    screen position so the view lines up with the player's eye) blended into a glowing rim ('Rim' colour). 'Open' = 0
    shows a solid swirl colour (unlinked portal)."""
    name = 'M_Portal'; path = FOLDER + '/' + name
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    m = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED); m.set_editor_property('two_sided', True)
    sp = MEL.create_material_expression(m, unreal.MaterialExpressionScreenPosition, -1200, 0)
    view = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    view.set_editor_property('parameter_name', 'View')
    view.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/DefaultTexture'))
    MEL.connect_material_expressions(sp, 'ViewportUV', view, 'UVs')
    uv = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1200, 400)
    c = MEL.create_material_expression(m, unreal.MaterialExpressionConstant2Vector, -1200, 520); c.set_editor_property('r', 0.5); c.set_editor_property('g', 0.5)
    dist = MEL.create_material_expression(m, unreal.MaterialExpressionDistance, -1000, 450)
    MEL.connect_material_expressions(uv, '', dist, 'A'); MEL.connect_material_expressions(c, '', dist, 'B')
    two = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -850, 450); two.set_editor_property('const_b', 2.0)
    MEL.connect_material_expressions(dist, '', two, 'A')
    # rim weight = saturate((d - 0.8) * 6)
    sub = MEL.create_material_expression(m, unreal.MaterialExpressionAdd, -700, 450); sub.set_editor_property('const_b', -0.8)
    MEL.connect_material_expressions(two, '', sub, 'A')
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 450); mul.set_editor_property('const_b', 6.0)
    MEL.connect_material_expressions(sub, '', mul, 'A')
    sat = MEL.create_material_expression(m, unreal.MaterialExpressionSaturate, -500, 450)
    MEL.connect_material_expressions(mul, '', sat, 'Input')
    rim = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 250)
    rim.set_editor_property('parameter_name', 'Rim'); rim.set_editor_property('default_value', unreal.LinearColor(0.1, 0.5, 1.0, 1.0))
    openp = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 650)
    openp.set_editor_property('parameter_name', 'Open'); openp.set_editor_property('default_value', 1.0)
    glow = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 750)
    glow.set_editor_property('parameter_name', 'Glow'); glow.set_editor_property('default_value', 3.0)
    rimg = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -700, 250)
    MEL.connect_material_expressions(rim, 'RGB', rimg, 'A'); MEL.connect_material_expressions(glow, '', rimg, 'B')
    # unlinked: solid rim colour inside
    inner = MEL.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -500, 100)
    MEL.connect_material_expressions(rim, 'RGB', inner, 'A'); MEL.connect_material_expressions(view, 'RGB', inner, 'B'); MEL.connect_material_expressions(openp, '', inner, 'Alpha')
    out = MEL.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -300, 200)
    MEL.connect_material_expressions(inner, '', out, 'A'); MEL.connect_material_expressions(rimg, '', out, 'B'); MEL.connect_material_expressions(sat, '', out, 'Alpha')
    MEL.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # oval mask: (1 - d) * 10
    om = MEL.create_material_expression(m, unreal.MaterialExpressionOneMinus, -500, 600)
    MEL.connect_material_expressions(two, '', om, 'Input')
    om10 = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 600); om10.set_editor_property('const_b', 10.0)
    MEL.connect_material_expressions(om, '', om10, 'A')
    MEL.connect_material_property(om10, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    # (rim feeds both lerps as RGB: a float4 parameter against the float3 view sample fails the SM5 compile)
    log('material M_Portal')


def main():
    fbx = os.path.join(BUILD, 'fbx')
    if not BUILD or not os.path.exists(os.path.join(fbx, 'Steve3D.fbx')):
        log('ERROR no Steve3D.fbx in %s' % fbx); return
    if EAL.does_directory_exist(FOLDER): EAL.delete_directory(FOLDER)
    EAL.make_directory(FOLDER)
    skin = pixel_texture(os.path.join(BUILD, 'T_Steve3D_Skin.png'), 'T_PPSteve_Skin')
    task(os.path.join(fbx, 'Steve3D.fbx'), 'SK_PPSteve', fbx_ui(True, True, False))
    sk = unreal.load_asset(FOLDER + '/SK_PPSteve')
    if not isinstance(sk, unreal.SkeletalMesh): log('ERROR SK_PPSteve import failed'); return
    skeleton = sk.get_editor_property('skeleton')
    pa = sk.get_editor_property('physics_asset')
    log('physics asset %s' % (pa.get_name() if pa else 'MISSING'))
    m = material('M_PPSteve', skin, 'Skin', masked=True)
    mats = sk.get_editor_property('materials')
    for i in range(len(mats)): mats[i].set_editor_property('material_interface', m)
    sk.set_editor_property('materials', mats); EAL.save_loaded_asset(sk)
    log('mesh SK_PPSteve bounds %s' % sk.get_imported_bounds().box_extent)
    ok = 0
    for c in CLIPS:
        f = os.path.join(fbx, 'P_%s.fbx' % c)
        if not os.path.exists(f): log('WARN missing clip P_%s' % c); continue
        want = FOLDER + '/P_' + c
        for p in task(f, 'P_' + c, fbx_ui(False, True, True, skeleton)):
            a = unreal.load_asset(p)
            if isinstance(a, unreal.AnimSequence) and p.split('.')[0] != want:
                if EAL.does_asset_exist(want): EAL.delete_asset(want)
                EAL.rename_asset(p.split('.')[0], want)
        if isinstance(unreal.load_asset(want), unreal.AnimSequence): ok += 1; log('clip P_%s' % c)
    portal_material()
    EAL.save_directory(FOLDER)
    log('DONE clips=%d/%d' % (ok, len(CLIPS)))


main()
