"""Offline UE4.27 editor step (UE4Editor-Cmd -run=pythonscript). Creates the project's original materials and empty host map.
No Minecraft assets: all textures arrive from the running Minecraft client at runtime.

Block shading reproduces vanilla's terrain shader:  colour = atlas * vertexTint * faceShade * Lightmap(blockLight, skyLight)
where Lightmap is Minecraft's live 16x16 LightTexture (sent by Java) sampled at UV1 = ((block+.5)/16, (sky+.5)/16).
That vanilla term is the emissive output (scaled so the host's exposure maps it 1:1); the UE sun adds a weighted
direct-light/contact-shadow layer through BaseColor, masked by Java sky light so caves are never sunlit."""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
FOLDER = '/Game/Crb'
WHITE = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')


def fresh(name):
    path = FOLDER + '/' + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def node(m, cls, x, y, **props):
    n = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    assert MEL.connect_material_expressions(a, out, b, inp), (a, out, b, inp)


def to_prop(a, out, prop):
    assert MEL.connect_material_property(a, out, prop), (a, out, prop)


def scalar(m, name, value, x, y):
    return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def mul(m, a, aout, b, bout, x, y):
    n = node(m, unreal.MaterialExpressionMultiply, x, y)
    link(a, aout, n, 'A'); link(b, bout, n, 'B')
    return n


def lerp(m, a, aout, b, bout, alpha, alphaout, x, y):
    n = node(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, aout, n, 'A'); link(b, bout, n, 'B'); link(alpha, alphaout, n, 'Alpha')
    return n


def srgb(m, a, aout, x, y):
    # Minecraft multiplies colours in display (sRGB) space; vertex colours arrive as raw 0..1 bytes, so decode them
    # to linear here (pow 2.2) - the replacing tonemapper re-encodes, making the products match vanilla 1:1.
    n = node(m, unreal.MaterialExpressionPower, x, y, const_exponent=2.2)
    link(a, aout, n, 'Base')
    return n


def vanilla_fog(m, x, y):
    """Vanilla terrain fog: linear in cylindrical (horizontal) camera distance between FogStart and FogEnd (UE units).
    Returns (fog_factor_node, fog_color_node)."""
    wp = node(m, unreal.MaterialExpressionWorldPosition, x, y)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, x, y + 100)
    wp2 = node(m, unreal.MaterialExpressionComponentMask, x + 200, y, r=True, g=True, b=False, a=False)
    link(wp, '', wp2, '')
    cam2 = node(m, unreal.MaterialExpressionComponentMask, x + 200, y + 100, r=True, g=True, b=False, a=False)
    link(cam, '', cam2, '')
    dist = node(m, unreal.MaterialExpressionDistance, x + 400, y)
    link(wp2, '', dist, 'A'); link(cam2, '', dist, 'B')
    start = scalar(m, 'FogStart', 1.0e7, x + 400, y + 120)
    end = scalar(m, 'FogEnd', 1.1e7, x + 400, y + 220)
    num = node(m, unreal.MaterialExpressionSubtract, x + 600, y)
    link(dist, '', num, 'A'); link(start, '', num, 'B')
    den = node(m, unreal.MaterialExpressionSubtract, x + 600, y + 150)
    link(end, '', den, 'A'); link(start, '', den, 'B')
    div = node(m, unreal.MaterialExpressionDivide, x + 800, y)
    link(num, '', div, 'A'); link(den, '', div, 'B')
    f = node(m, unreal.MaterialExpressionSaturate, x + 1000, y)
    link(div, '', f, '')
    col = node(m, unreal.MaterialExpressionVectorParameter, x + 800, y + 260, parameter_name='FogColor', default_value=unreal.LinearColor(0.7, 0.8, 1.0, 1))
    return f, col


def tex_param(m, name, x, y, coord=0, split=False):
    t = node(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=WHITE)
    if coord:
        t.set_editor_property('const_coordinate', coord)
    if split:
        # Atlas UV = TexCoord0 (coarse, 1/256 steps) + TexCoord2 (fine remainder): half-precision vertex UVs alone
        # snap to 4 texels in an 8192-wide atlas (see CrbSplitUV in CrbCoords.h). Meshes without UV2 read 0.
        c0 = node(m, unreal.MaterialExpressionTextureCoordinate, x - 400, y + 60, coordinate_index=0)
        c2 = node(m, unreal.MaterialExpressionTextureCoordinate, x - 400, y + 160, coordinate_index=2)
        add = node(m, unreal.MaterialExpressionAdd, x - 200, y + 100)
        link(c0, '', add, 'A'); link(c2, '', add, 'B'); link(add, '', t, 'UVs')
    return t


def finish(m, name):
    MEL.recompile_material(m)
    EAL.save_asset(FOLDER + '/' + name)
    unreal.log('CRB ASSET OK ' + name)


def block(name, translucent):
    m = fresh(name)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('opacity_mask_clip_value', 0.1)
    if translucent:
        m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE)
    atlas = tex_param(m, 'Atlas', -1400, -200, split=True)
    white = node(m, unreal.MaterialExpressionConstant3Vector, -1400, -420, constant=unreal.LinearColor(1, 1, 1, 1))
    one = node(m, unreal.MaterialExpressionConstant, -1400, -520, r=1.0)
    has_atlas = scalar(m, 'HasAtlas', 0.0, -1400, 40)
    albedo = lerp(m, white, '', atlas, 'RGB', has_atlas, '', -1100, -260)
    alpha = lerp(m, one, '', atlas, 'A', has_atlas, '', -1100, -60)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1600, 200)
    vc_rgb = srgb(m, vc, '', -1400, 200)
    vc_a = srgb(m, vc, 'A', -1400, 300)
    tinted = mul(m, albedo, '', vc_rgb, '', -850, -220)           # atlas * biome tint
    shaded = mul(m, tinted, '', vc_a, '', -650, -220)             # * vanilla face shade/AO (vertex alpha)
    lm = tex_param(m, 'Lightmap', -1400, 420, coord=1)            # Minecraft LightTexture at (block, sky)
    use_lm = scalar(m, 'UseLightmap', 1.0, -1100, 600)
    lm_sel = lerp(m, white, '', lm, 'RGB', use_lm, '', -850, 420)
    vanilla = mul(m, shaded, '', lm_sel, '', -450, -100)          # == vanilla terrain colour
    e_scale = scalar(m, 'EmissiveScale', 3183.0, -450, 120)
    v_weight = scalar(m, 'VanillaWeight', 0.85, -450, 220)
    e1 = mul(m, vanilla, '', e_scale, '', -200, -100)
    emissive = mul(m, e1, '', v_weight, '', 0, -100)
    # Vanilla distance fog over the render-distance edge (fog colour from Java, same radiance scale).
    fog, fog_col = vanilla_fog(m, -1400, 1100)
    fog_e = mul(m, fog_col, '', e_scale, '', 0, 1300)
    emissive_f = lerp(m, emissive, '', fog_e, '', fog, '', 200, -100)
    to_prop(emissive_f, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    keep = node(m, unreal.MaterialExpressionOneMinus, 200, 1100)
    link(fog, '', keep, '')
    # Direct sun layer: albedo * tint * SunWeight * skyLight (caves stay unlit by the sun).
    uv1 = node(m, unreal.MaterialExpressionTextureCoordinate, -1100, 820, coordinate_index=1)
    sky = node(m, unreal.MaterialExpressionComponentMask, -850, 820, r=False, g=True, b=False, a=False)
    link(uv1, '', sky, '')
    sun_w = scalar(m, 'SunWeight', 0.3, -850, 940)
    b1 = mul(m, tinted, '', sun_w, '', -450, 600)
    base = mul(m, b1, '', sky, '', -200, 600)
    to_prop(mul(m, base, '', keep, '', 300, 600), '', unreal.MaterialProperty.MP_BASE_COLOR)
    to_prop(node(m, unreal.MaterialExpressionConstant, -200, 800, r=0.9), '', unreal.MaterialProperty.MP_ROUGHNESS)
    to_prop(node(m, unreal.MaterialExpressionConstant, -200, 880, r=0.25), '', unreal.MaterialProperty.MP_SPECULAR)
    to_prop(alpha, '', unreal.MaterialProperty.MP_OPACITY if translucent else unreal.MaterialProperty.MP_OPACITY_MASK)
    finish(m, name)


def entity(name, translucent):
    m = fresh(name)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('opacity_mask_clip_value', 0.1)
    if translucent:
        m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE)
    tex = tex_param(m, 'Tex', -1200, -200, split=True)
    white = node(m, unreal.MaterialExpressionConstant3Vector, -1200, -420, constant=unreal.LinearColor(0.8, 0.8, 0.8, 1))
    one = node(m, unreal.MaterialExpressionConstant, -1200, -520, r=1.0)
    has = scalar(m, 'HasTexture', 1.0, -1200, 40)
    albedo = lerp(m, white, '', tex, 'RGB', has, '', -900, -260)
    a = lerp(m, one, '', tex, 'A', has, '', -900, -60)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1400, 200)
    col = mul(m, albedo, '', srgb(m, vc, '', -1200, 200), '', -700, -220)
    alpha = mul(m, a, '', vc, 'A', -700, 0)
    bright = scalar(m, 'Bright', 1.0, -700, 200)
    e_scale = scalar(m, 'EmissiveScale', 3183.0, -700, 300)
    v_weight = scalar(m, 'VanillaWeight', 0.85, -700, 400)
    e1 = mul(m, col, '', bright, '', -450, -200)
    e2 = mul(m, e1, '', e_scale, '', -250, -200)
    emissive = mul(m, e2, '', v_weight, '', -50, -200)
    to_prop(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    sun_w = scalar(m, 'SunWeight', 0.3, -450, 400)
    base = mul(m, col, '', sun_w, '', -250, 300)
    to_prop(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    to_prop(node(m, unreal.MaterialExpressionConstant, -250, 500, r=0.85), '', unreal.MaterialProperty.MP_ROUGHNESS)
    to_prop(alpha, '', unreal.MaterialProperty.MP_OPACITY if translucent else unreal.MaterialProperty.MP_OPACITY_MASK)
    finish(m, name)


def particle():
    name = 'M_CrbParticle'
    m = fresh(name)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    tex = tex_param(m, 'Tex', -1000, -200, split=True)
    white = node(m, unreal.MaterialExpressionConstant3Vector, -1000, -420, constant=unreal.LinearColor(1, 1, 1, 1))
    one = node(m, unreal.MaterialExpressionConstant, -1000, -520, r=1.0)
    has = scalar(m, 'HasTexture', 0.0, -1000, 40)
    albedo = lerp(m, white, '', tex, 'RGB', has, '', -700, -260)
    a = lerp(m, one, '', tex, 'A', has, '', -700, -60)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1200, 200)
    col = mul(m, albedo, '', srgb(m, vc, '', -1000, 200), '', -500, -220)
    alpha = mul(m, a, '', vc, 'A', -500, 0)
    e_scale = scalar(m, 'EmissiveScale', 3183.0, -500, 200)
    to_prop(mul(m, col, '', e_scale, '', -250, -200), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    to_prop(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    finish(m, name)


def unlit_color(name, blend, default_scale):
    m = fresh(name)
    m.set_editor_property('blend_mode', blend)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    color = node(m, unreal.MaterialExpressionVectorParameter, -600, 0, parameter_name='Color', default_value=unreal.LinearColor(0.47, 0.65, 1.0, 1))
    scale = scalar(m, 'Intensity', default_scale, -600, 200)
    to_prop(mul(m, color, '', scale, '', -300, 0), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if blend == unreal.BlendMode.BLEND_ADDITIVE:
        to_prop(node(m, unreal.MaterialExpressionConstant, -300, 200, r=1.0), '', unreal.MaterialProperty.MP_OPACITY)
    finish(m, name)


def outline():
    # LevelRenderer hit outline: black lines at 40% opacity, depth tested (hidden edges are not drawn).
    name = 'M_CrbOutline'
    m = fresh(name)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    to_prop(node(m, unreal.MaterialExpressionConstant3Vector, -400, 0, constant=unreal.LinearColor(0, 0, 0, 1)), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    to_prop(scalar(m, 'Opacity', 0.4, -400, 200), '', unreal.MaterialProperty.MP_OPACITY)
    finish(m, name)


def sky_dome():
    # Vanilla sky: the sky colour fades into the fog colour toward the horizon. Vanilla draws a sky plane 16 blocks
    # above the camera with spherical fog ending at the render distance, so fog = saturate(SkyFogScale / dir.z) with
    # SkyFogScale = 16 / renderDistanceBlocks; below the horizon it is pure fog colour.
    name = 'M_CrbSky'
    m = fresh(name)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    color = node(m, unreal.MaterialExpressionVectorParameter, -600, 0, parameter_name='Color', default_value=unreal.LinearColor(0.47, 0.65, 1.0, 1))
    fogc = node(m, unreal.MaterialExpressionVectorParameter, -600, 150, parameter_name='FogColor', default_value=unreal.LinearColor(0.7, 0.8, 1.0, 1))
    scale = scalar(m, 'Intensity', 3183.0, -600, 300)
    k = scalar(m, 'SkyFogScale', 0.1667, -1000, 500)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1600, 400)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1600, 500)
    d = node(m, unreal.MaterialExpressionSubtract, -1400, 400)
    link(wp, '', d, 'A'); link(cam, '', d, 'B')
    n = node(m, unreal.MaterialExpressionNormalize, -1250, 400)
    link(d, '', n, '')
    z = node(m, unreal.MaterialExpressionComponentMask, -1100, 400, r=False, g=False, b=True, a=False)
    link(n, '', z, '')
    zc = node(m, unreal.MaterialExpressionMax, -950, 400, const_b=0.001)
    link(z, '', zc, 'A')
    q = node(m, unreal.MaterialExpressionDivide, -800, 450)
    link(k, '', q, 'A'); link(zc, '', q, 'B')
    f = node(m, unreal.MaterialExpressionSaturate, -650, 450)
    link(q, '', f, '')
    c = lerp(m, color, '', fogc, '', f, '', -400, 100)
    to_prop(mul(m, c, '', scale, '', -200, 100), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m, name)


def tonemap():
    # Replaces UE's filmic tonemapper so the frame is encoded like Minecraft's: display = (scene * exposure)^(1/2.2).
    # (ACES filmic lifts blacks and desaturates; Minecraft writes its sRGB-space products straight to the screen.)
    name = 'M_CrbTonemap'
    m = fresh(name)
    m.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property('blendable_location', unreal.BlendableLocation.BL_REPLACING_TONEMAPPER)
    st = node(m, unreal.MaterialExpressionSceneTexture, -900, 0, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    mask = node(m, unreal.MaterialExpressionComponentMask, -700, 0, r=True, g=True, b=True, a=False)
    link(st, 'Color', mask, '')
    ea = node(m, unreal.MaterialExpressionEyeAdaptation, -700, 200)
    exposed = mul(m, mask, '', ea, '', -500, 0)
    sat = node(m, unreal.MaterialExpressionSaturate, -350, 0)
    link(exposed, '', sat, '')
    enc = node(m, unreal.MaterialExpressionPower, -200, 0, const_exponent=1.0 / 2.2)
    link(sat, '', enc, 'Base')
    to_prop(enc, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m, name)


def vertex_color_lit():
    name = 'M_CrbVertexColor'
    m = fresh(name)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    vc = node(m, unreal.MaterialExpressionVertexColor, -600, 0)
    to_prop(vc, '', unreal.MaterialProperty.MP_BASE_COLOR)
    e = scalar(m, 'EmissiveScale', 1500.0, -600, 200)
    to_prop(mul(m, vc, '', e, '', -300, 100), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    to_prop(node(m, unreal.MaterialExpressionConstant, -300, 300, r=0.45), '', unreal.MaterialProperty.MP_ROUGHNESS)
    finish(m, name)


block('M_CrbBlock', False)
block('M_CrbBlockTranslucent', True)
entity('M_CrbEntity', False)
entity('M_CrbEntityTranslucent', True)
particle()
unlit_color('M_CrbEmissive', unreal.BlendMode.BLEND_ADDITIVE, 4500.0)
sky_dome()
vertex_color_lit()
outline()
tonemap()

MAP = FOLDER + '/HostWorld'
if EAL.does_asset_exist(MAP):
    assert unreal.EditorLevelLibrary.load_level(MAP)
else:
    assert unreal.EditorLevelLibrary.new_level(MAP)
assert not any(isinstance(a, (unreal.StaticMeshActor, unreal.Brush)) for a in unreal.EditorLevelLibrary.get_all_level_actors())
assert unreal.EditorLevelLibrary.save_current_level()
unreal.log('CRB ASSETS COMPLETE')
