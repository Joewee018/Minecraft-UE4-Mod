"""SM64 Steve Movement - Steve rig and animation build (Blender 3.6, headless). Everything here is original:
the mesh is the standard Minecraft player box layout (so any 64x64 skin maps onto it), the skeleton is a simple
hierarchy for those boxes, and every clip is keyframed by this script. No Mario model or animation data is used.
  blender -b --factory-startup -P steve_build.py -- <out_dir>

Outputs in <out_dir>:
  fbx/Steve.fbx         skinned mesh: classic + slim arm sets (slim bones hidden at runtime for wide skins and
                        vice versa), material slots Base (opaque) and Overlay (hat/jacket/sleeves/pants, masked)
  fbx/S_<Clip>.fbx      one clip per file on the same armature (30 fps)
  T_SteveSkin_Default.png  an original placeholder skin (the game's real skin replaces it at runtime)
  skeleton.json, clips.json, preview_*.png

Frame: Blender Z up, the character faces -Y, 1 skin pixel = 1.8 m / 32 px."""
import bpy, json, math, os, sys, zlib
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]
skin_src = argv[1] if len(argv) > 1 and os.path.exists(argv[1]) else ''   # the real Steve skin from the local Minecraft jar
fbx_dir = os.path.join(out, 'fbx'); os.makedirs(fbx_dir, exist_ok=True)
PX = 1.8 / 32.0
FPS = 30

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS

# ------------------------------------------------------------------ placeholder skin (original pixel art)
def make_default_skin(path):
    W = 64
    px = [(0, 0, 0, 0)] * (W * W)
    def rect(x0, y0, w, h, c):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                px[y * W + x] = c
    skin, hair, shirt, pants, shoe, eye = (0.78, 0.6, 0.47, 1), (0.28, 0.18, 0.1, 1), (0.2, 0.55, 0.62, 1), (0.25, 0.25, 0.55, 1), (0.3, 0.3, 0.32, 1), (0.25, 0.3, 0.75, 1)
    rect(0, 0, 32, 16, skin); rect(8, 0, 8, 8, hair); rect(0, 8, 32, 2, hair); rect(24, 8, 8, 8, hair)  # head
    rect(9, 12, 2, 1, (1, 1, 1, 1)); rect(10, 12, 1, 1, eye); rect(13, 12, 2, 1, (1, 1, 1, 1)); rect(13, 12, 1, 1, eye)
    rect(11, 14, 2, 1, (0.5, 0.3, 0.25, 1))
    rect(16, 16, 24, 16, shirt)                                         # body
    for (u, v) in ((40, 16), (32, 48)):                                 # arms: sleeve top, skin below
        rect(u, v, 16, 16, skin); rect(u, v, 16, 8, shirt)
    for (u, v) in ((0, 16), (16, 48)):                                  # legs: pants, shoes
        rect(u, v, 16, 16, pants); rect(u, v + 13, 16, 3, shoe)
    img = bpy.data.images.new('T_SteveSkin_Default', W, W, alpha=True)
    flat = []
    for y in range(W - 1, -1, -1):          # Blender images are bottom-up
        for x in range(W):
            flat.extend(px[y * W + x])
    img.pixels = flat
    img.filepath_raw = path; img.file_format = 'PNG'; img.save()
    return img

if skin_src:
    # Default texture = Minecraft's own Steve skin (textures/entity/player/wide/steve.png), read from the user's local
    # Minecraft 1.20.1 jar at build time; stays in the private Work/ build. At runtime the live player skin replaces it.
    import shutil
    _dst = os.path.join(out, 'T_SteveSkin_Default.png'); shutil.copyfile(skin_src, _dst)
    _img = bpy.data.images.load(_dst); _img.name = 'T_SteveSkin_Default'
    print('STEVE skin from', skin_src, tuple(_img.size))
else:
    make_default_skin(os.path.join(out, 'T_SteveSkin_Default.png'))
    print('WARN steve.png not found; placeholder skin used')

# ------------------------------------------------------------------ armature
# Every bone points straight up from its pivot with roll 0, so all pose bones share the armature frame:
#   local X = +X (character's left), local Y = up, local Z = -Y (forward).
#   rot X > 0: the bone's up end tips forward (a hanging arm/leg swings BACK; negative swings it forward/up).
#   rot Y > 0: turns toward the character's left.   rot Z > 0: a hanging limb swings toward +X, so
#   'outward' is Z < 0 for the right-side limbs and Z > 0 for the left-side ones (Z is applied first: ZYX mode).
# Mario-style proportions, still all hard-edged Minecraft boxes: big cube head, short stout body, stubby legs,
# block hands and block feet (the sm5 layout the user preferred; about 30 px tall, Unreal scales it to the chosen height).
BONES = [  # name, parent, pivot (px: x, y, z)
    ('root', None, (0, 0, 0)),
    ('pelvis', 'root', (0, 0, 11)),
    ('body', 'pelvis', (0, 0, 9)),
    ('head', 'body', (0, 0, 18)),
    ('arm_r', 'body', (-7.2, 0, 16)), ('arm_l', 'body', (7.2, 0, 16)),
    ('arm_r_slim', 'body', (-6.7, 0, 16)), ('arm_l_slim', 'body', (6.7, 0, 16)),
    ('leg_r', 'pelvis', (-2.4, 0, 9)), ('leg_l', 'pelvis', (2.4, 0, 9)),
]
arm_data = bpy.data.armatures.new('SteveRig')
arm = bpy.data.objects.new('Armature', arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
for name, parent, (x, y, z) in BONES:
    b = arm_data.edit_bones.new(name)
    b.head = Vector((x * PX, y * PX, z * PX))
    b.tail = b.head + Vector((0, 0, 4 * PX if name != 'root' else 2 * PX))
    b.roll = 0.0
    if parent: b.parent = arm_data.edit_bones[parent]
    b.use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')

# ------------------------------------------------------------------ mesh: Minecraft player boxes
verts, faces, uvs, mats, groups = [], [], [], [], []

def box(origin, msize, tex, ssize, bone, rows=None, top=True, bottom=True, inflate=0.0, overlay=False):
    """A hard-edged box. origin = min corner in px (-X is the character's right, -Y the front), msize = mesh size
    (w along X, h along Z, d along Y), tex = (u, v) skin offset, ssize = the skin part's (w, h, d) in pixels.
    rows = (r0, r1): which skin rows of the part's sides this box shows (a limb split into arm + hand)."""
    w, h, d = msize
    sw, sh, sd = ssize
    r0, r1 = rows or (0, sh)
    x0, y0, z0 = origin[0] - inflate, origin[1] - inflate, origin[2] - inflate
    x1, y1, z1 = origin[0] + w + inflate, origin[1] + d + inflate, origin[2] + h + inflate
    u, v = tex
    P = lambda x, y, z: Vector((x * PX, y * PX, z * PX))
    vs = v + sd + r0
    spec = [
        ((P(x0, y0, z1), P(x1, y0, z1), P(x1, y0, z0), P(x0, y0, z0)), (u + sd, vs, sw, r1 - r0), Vector((0, -1, 0))),            # front
        ((P(x0, y1, z1), P(x0, y0, z1), P(x0, y0, z0), P(x0, y1, z0)), (u, vs, sd, r1 - r0), Vector((-1, 0, 0))),                 # right side
        ((P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0), P(x1, y0, z0)), (u + sd + sw, vs, sd, r1 - r0), Vector((1, 0, 0))),        # left side
        ((P(x1, y1, z1), P(x0, y1, z1), P(x0, y1, z0), P(x1, y1, z0)), (u + 2 * sd + sw, vs, sw, r1 - r0), Vector((0, 1, 0))),    # back
    ]
    if top: spec.append(((P(x0, y1, z1), P(x1, y1, z1), P(x1, y0, z1), P(x0, y0, z1)), (u + sd, v, sw, sd), Vector((0, 0, 1))))
    if bottom: spec.append(((P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0)), (u + sd + sw, v, sw, sd), Vector((0, 0, -1))))
    for corners, (fu, fv, fw, fh), n in spec:
        uv = [(fu, fv), (fu + fw, fv), (fu + fw, fv + fh), (fu, fv + fh)]
        order = [0, 1, 2, 3]
        nrm = (corners[1] - corners[0]).cross(corners[3] - corners[0])
        if nrm.dot(n) < 0: order = [0, 3, 2, 1]
        base = len(verts)
        for i in order:
            verts.append(corners[i]); uvs.append((uv[i][0] / 64.0, 1.0 - uv[i][1] / 64.0)); groups.append(bone)
        faces.append([base, base + 1, base + 2, base + 3]); mats.append(1 if overlay else 0)


def part(origin, msize, tex, tex2, ssize, bone, infl, rows=None, top=True, bottom=True):
    box(origin, msize, tex, ssize, bone, rows, top, bottom)
    box(origin, msize, tex2, ssize, bone, rows, top, bottom, infl, True)


# head: big cube (skin 8x8x8 shown at 11.5 px)
part((-5.75, -5.75, 18), (11.5, 11.5, 11.5), (0, 0), (32, 0), (8, 8, 8), 'head', 0.6)
# body: short and stout (skin 8x12x4)
part((-5, -3.5, 9), (10, 9, 7), (16, 16), (16, 32), (8, 12, 4), 'body', 0.3)
# arms: sleeve block (skin rows 0-8) + bigger hand block (rows 8-12)
for bone, x, w, hx, hw, tex, tex2, sw in (('arm_r', -9.2, 4.0, -9.5, 4.6, (40, 16), (40, 32), 4), ('arm_l', 5.2, 4.0, 4.9, 4.6, (32, 48), (48, 48), 4),
                                         ('arm_r_slim', -8.2, 3.0, -8.5, 3.6, (40, 16), (40, 32), 3), ('arm_l_slim', 5.2, 3.0, 4.9, 3.6, (32, 48), (48, 48), 3)):
    part((x, -2.0, 11.4), (w, 6.6, 4.0), tex, tex2, (sw, 12, 4), bone, 0.3, rows=(0, 8), bottom=False)
    part((hx, -2.3, 7.4), (hw, 4.0, 4.6), tex, tex2, (sw, 12, 4), bone, 0.3, rows=(8, 12), top=False)
# legs: stubby leg block (rows 0-9) + big foot block sticking out front (rows 9-12)
for bone, x, tex, tex2 in (('leg_r', -4.3, (0, 16), (0, 32)), ('leg_l', 0.5, (16, 48), (0, 48))):
    part((x, -1.9, 2.6), (3.8, 6.6, 3.8), tex, tex2, (4, 12, 4), bone, 0.3, rows=(0, 9), bottom=False)
    part((x - 0.4, -4.4, 0), (4.6, 2.6, 6.6), tex, tex2, (4, 12, 4), bone, 0.3, rows=(9, 12), top=False)

me = bpy.data.meshes.new('SteveMesh')
me.from_pydata([tuple(v) for v in verts], [], faces)
me.update()
uvl = me.uv_layers.new(name='UVMap')
for poly in me.polygons:
    poly.material_index = mats[poly.index]
    for li in poly.loop_indices:
        uvl.data[li].uv = uvs[me.loops[li].vertex_index]
for name in ('Base', 'Overlay'):
    m = bpy.data.materials.new(name); me.materials.append(m)
mesh = bpy.data.objects.new('Steve', me)
scene.collection.objects.link(mesh)
for name, _, _ in BONES:
    mesh.vertex_groups.new(name=name)
for vi, g in enumerate(groups):
    mesh.vertex_groups[g].add([vi], 1.0, 'REPLACE')
mesh.parent = arm
mod = mesh.modifiers.new('Armature', 'ARMATURE'); mod.object = arm
print('STEVE mesh verts=%d faces=%d' % (len(verts), len(faces)))

# ------------------------------------------------------------------ animation helpers
for pb in arm.pose.bones: pb.rotation_mode = 'ZYX'   # Z (spread) applied first, then Y, then X (swing)
arm.animation_data_create()
D = math.radians
ARMS = {'arm_r': 'arm_r_slim', 'arm_l': 'arm_l_slim'}


def clip(name, length, keys, loop=False):
    """keys: {frame: {bone: {'r': (x, y, z) degrees, 'l': (x, y, z) px offset}}}; unspecified bones rest.
    Slim arm bones copy the classic arm keys. A looping clip repeats its first key at the end."""
    act = bpy.data.actions.new('S_' + name)
    arm.animation_data.action = act
    frames = sorted(keys)
    if loop and length not in keys: keys[length] = keys[frames[0]]; frames = sorted(keys)
    for f in frames:
        pose = keys[f]
        for pb in arm.pose.bones:
            src = pb.name
            for c, s in ARMS.items():
                if pb.name == s: src = c
            k = pose.get(src, {})
            r = k.get('r', (0, 0, 0)); l = k.get('l', (0, 0, 0))
            pb.rotation_euler = (D(r[0]), D(r[1]), D(r[2]))
            pb.location = (l[0] * PX, l[2] * PX, l[1] * PX)    # bone local axes: X, Y=up, Z=forward; l = (x, forward, up) px
            pb.keyframe_insert('rotation_euler', frame=f)
            pb.keyframe_insert('location', frame=f)
    for fc in act.fcurves:
        for kp in fc.keyframe_points: kp.interpolation = 'BEZIER'; kp.easing = 'AUTO'
    act['crb_len'] = length
    return act, length


def pose(**bones):
    return {b: v for b, v in bones.items()}


CLIPS = []

def add(name, length, keys, loop=False):
    CLIPS.append((name, length, keys, loop))

# Idle: slow breathing, head glance, arms sway.
add('Idle', 60, {
    0: pose(body={'r': (0, 0, 0)}, head={'r': (0, 0, 0)}, arm_r={'r': (0, 0, -3)}, arm_l={'r': (0, 0, 3)}),
    30: pose(body={'r': (1.5, 0, 0)}, head={'r': (-2, 6, 0)}, arm_r={'r': (2, 0, -5)}, arm_l={'r': (-2, 0, 5)}, pelvis={'l': (0, 0, -0.15)}),
}, loop=True)
# Walk (2 steps / 24 frames).
add('Walk', 24, {
    0: pose(leg_r={'r': (-30, 0, 0)}, leg_l={'r': (30, 0, 0)}, arm_r={'r': (30, 0, -4)}, arm_l={'r': (-30, 0, 4)}, pelvis={'l': (0, 0, 0)}),
    6: pose(leg_r={'r': (0, 0, 0)}, leg_l={'r': (0, 0, 0)}, arm_r={'r': (0, 0, -4)}, arm_l={'r': (0, 0, 4)}, pelvis={'l': (0, 0, 0.35)}, body={'r': (3, 0, 0)}),
    12: pose(leg_r={'r': (30, 0, 0)}, leg_l={'r': (-30, 0, 0)}, arm_r={'r': (-30, 0, -4)}, arm_l={'r': (30, 0, 4)}, pelvis={'l': (0, 0, 0)}),
    18: pose(leg_r={'r': (0, 0, 0)}, leg_l={'r': (0, 0, 0)}, arm_r={'r': (0, 0, -4)}, arm_l={'r': (0, 0, 4)}, pelvis={'l': (0, 0, 0.35)}, body={'r': (3, 0, 0)}),
}, loop=True)
# Run: big strides, forward lean, arms pumping.
add('Run', 16, {
    0: pose(leg_r={'r': (-65, 0, 0)}, leg_l={'r': (45, 0, 0)}, arm_r={'r': (60, 0, -8)}, arm_l={'r': (-75, 0, 8)}, body={'r': (14, -6, 0)}, head={'r': (-10, 6, 0)}, pelvis={'l': (0, 0, -0.3)}),
    4: pose(leg_r={'r': (-10, 0, 0)}, leg_l={'r': (5, 0, 0)}, arm_r={'r': (5, 0, -8)}, arm_l={'r': (-10, 0, 8)}, body={'r': (16, 0, 0)}, head={'r': (-12, 0, 0)}, pelvis={'l': (0, 0, 1.0)}),
    8: pose(leg_r={'r': (45, 0, 0)}, leg_l={'r': (-65, 0, 0)}, arm_r={'r': (-75, 0, -8)}, arm_l={'r': (60, 0, 8)}, body={'r': (14, 6, 0)}, head={'r': (-10, -6, 0)}, pelvis={'l': (0, 0, -0.3)}),
    12: pose(leg_r={'r': (5, 0, 0)}, leg_l={'r': (-10, 0, 0)}, arm_r={'r': (-10, 0, -8)}, arm_l={'r': (5, 0, 8)}, body={'r': (16, 0, 0)}, head={'r': (-12, 0, 0)}, pelvis={'l': (0, 0, 1.0)}),
}, loop=True)
# Skid: turning around at speed - leaning back, front leg planted, arms flung out.
SKID = pose(body={'r': (-22, 25, 0)}, head={'r': (10, -20, 0)}, leg_r={'r': (-45, 0, -8)}, leg_l={'r': (20, 0, 5)},
            arm_r={'r': (-30, 0, -70)}, arm_l={'r': (-40, 0, 75)}, pelvis={'l': (0, 0, -1.5)})
add('Skid', 10, {0: SKID, 5: pose(**{**SKID, 'pelvis': {'l': (0, 0, -1.8)}}), 10: SKID})
# Brake: stopping from a run - lean back.
BRAKE = pose(body={'r': (-14, 0, 0)}, head={'r': (6, 0, 0)}, leg_r={'r': (-30, 0, 0)}, leg_l={'r': (10, 0, 0)}, arm_r={'r': (-25, 0, -20)}, arm_l={'r': (-25, 0, 20)}, pelvis={'l': (0, 0, -1.0)})
add('Brake', 10, {0: BRAKE, 10: BRAKE})
# Crouch (hold).
CROUCH = pose(body={'r': (28, 0, 0)}, head={'r': (-22, 0, 0)}, leg_r={'r': (-35, 0, -3)}, leg_l={'r': (-35, 0, 3)}, arm_r={'r': (-25, 0, -6)}, arm_l={'r': (-25, 0, 6)}, pelvis={'l': (0, 0, -3.5)})
add('Crouch', 10, {0: CROUCH, 10: CROUCH})
# Crouch slide: low, one leg forward, arms back.
SLIDE = pose(body={'r': (38, 0, 0)}, head={'r': (-30, 0, 0)}, leg_r={'r': (-70, 0, 0)}, leg_l={'r': (25, 0, 0)}, arm_r={'r': (50, 0, -15)}, arm_l={'r': (50, 0, 15)}, pelvis={'l': (0, 0, -5)})
add('CrouchSlide', 10, {0: SLIDE, 10: SLIDE})
# Jump: one fist up, knee up.
add('Jump', 18, {
    0: pose(body={'r': (8, 0, 0)}, leg_r={'r': (-20, 0, 0)}, leg_l={'r': (10, 0, 0)}, arm_r={'r': (-40, 0, 0)}, arm_l={'r': (20, 0, 0)}, pelvis={'l': (0, 0, -1)}),
    5: pose(body={'r': (-4, 0, 0)}, leg_r={'r': (-55, 0, 0)}, leg_l={'r': (15, 0, 0)}, arm_r={'r': (-165, 0, -10)}, arm_l={'r': (25, 0, 15)}, head={'r': (-10, 0, 0)}),
    18: pose(body={'r': (0, 0, 0)}, leg_r={'r': (-35, 0, 0)}, leg_l={'r': (10, 0, 0)}, arm_r={'r': (-150, 0, -10)}, arm_l={'r': (20, 0, 15)}, head={'r': (-5, 0, 0)}),
})
# Double jump: both arms up and out, legs spread.
add('DoubleJump', 20, {
    0: pose(body={'r': (6, 0, 0)}, arm_r={'r': (-60, 0, -20)}, arm_l={'r': (-60, 0, 20)}, leg_r={'r': (-30, 0, 0)}),
    6: pose(body={'r': (-6, 0, 0)}, head={'r': (-15, 0, 0)}, arm_r={'r': (-170, 0, -35)}, arm_l={'r': (-170, 0, 35)}, leg_r={'r': (-40, 0, -12)}, leg_l={'r': (20, 0, 12)}),
    20: pose(body={'r': (0, 0, 0)}, arm_r={'r': (-155, 0, -40)}, arm_l={'r': (-155, 0, 40)}, leg_r={'r': (-25, 0, -10)}, leg_l={'r': (15, 0, 10)}),
})
# Triple jump: a forward somersault with a tuck, opening up at the end.
TUCK = dict(leg_r={'r': (-95, 0, 0)}, leg_l={'r': (-95, 0, 0)}, arm_r={'r': (-70, 0, 10)}, arm_l={'r': (-70, 0, -10)}, body={'r': (25, 0, 0)}, head={'r': (-10, 0, 0)})
add('TripleJump', 34, {
    0: pose(arm_r={'r': (-150, 0, -20)}, arm_l={'r': (-150, 0, 20)}, leg_r={'r': (-20, 0, 0)}),
    5: pose(pelvis={'r': (60, 0, 0)}, **TUCK),
    10: pose(pelvis={'r': (150, 0, 0)}, **TUCK),
    15: pose(pelvis={'r': (240, 0, 0)}, **TUCK),
    20: pose(pelvis={'r': (330, 0, 0)}, **TUCK),
    24: pose(pelvis={'r': (360, 0, 0)}, arm_r={'r': (-120, 0, -50)}, arm_l={'r': (-120, 0, 50)}, leg_r={'r': (-20, 0, -10)}, leg_l={'r': (10, 0, 10)}),
    34: pose(pelvis={'r': (360, 0, 0)}, arm_r={'r': (-100, 0, -70)}, arm_l={'r': (-100, 0, 70)}, leg_r={'r': (-15, 0, -8)}, leg_l={'r': (10, 0, 8)}),
})
# Backflip: a backward somersault from the crouch.
add('Backflip', 30, {
    0: pose(**{**CROUCH}),
    4: pose(pelvis={'r': (-30, 0, 0), 'l': (0, 0, 2)}, arm_r={'r': (-170, 0, -15)}, arm_l={'r': (-170, 0, 15)}, body={'r': (-15, 0, 0)}, head={'r': (20, 0, 0)}),
    9: pose(pelvis={'r': (-120, 0, 0), 'l': (0, 0, 3)}, **TUCK),
    14: pose(pelvis={'r': (-210, 0, 0), 'l': (0, 0, 3)}, **TUCK),
    19: pose(pelvis={'r': (-300, 0, 0), 'l': (0, 0, 2)}, **TUCK),
    24: pose(pelvis={'r': (-360, 0, 0)}, arm_r={'r': (-60, 0, -40)}, arm_l={'r': (-60, 0, 40)}, leg_r={'r': (-15, 0, 0)}),
    30: pose(pelvis={'r': (-360, 0, 0)}, arm_r={'r': (-40, 0, -30)}, arm_l={'r': (-40, 0, 30)}),
})
# Side flip: a cartwheel-like spin around the forward axis.
SPREAD = dict(arm_r={'r': (0, 0, -95)}, arm_l={'r': (0, 0, 95)}, leg_r={'r': (0, 0, -25)}, leg_l={'r': (0, 0, 25)})
add('Sideflip', 30, {
    0: pose(**SKID),
    4: pose(pelvis={'r': (0, 0, 40), 'l': (0, 0, 2)}, **SPREAD),
    10: pose(pelvis={'r': (0, 0, 140), 'l': (0, 0, 3)}, **SPREAD),
    16: pose(pelvis={'r': (0, 0, 250), 'l': (0, 0, 3)}, **SPREAD),
    22: pose(pelvis={'r': (0, 0, 340), 'l': (0, 0, 1)}, **SPREAD),
    25: pose(pelvis={'r': (0, 0, 360)}, arm_r={'r': (-40, 0, -50)}, arm_l={'r': (-40, 0, 50)}),
    30: pose(pelvis={'r': (0, 0, 360)}, arm_r={'r': (-30, 0, -40)}, arm_l={'r': (-30, 0, 40)}),
})
# Long jump: stretched out, arms forward like a dive.
LONG = pose(pelvis={'r': (55, 0, 0)}, body={'r': (10, 0, 0)}, head={'r': (-55, 0, 0)}, arm_r={'r': (-170, 0, -8)}, arm_l={'r': (-170, 0, 8)}, leg_r={'r': (15, 0, 0)}, leg_l={'r': (30, 0, 0)})
add('LongJump', 20, {0: pose(**SLIDE), 6: LONG, 20: pose(**{**LONG, 'leg_r': {'r': (5, 0, 0)}, 'leg_l': {'r': (35, 0, 0)}})})
# Fall: arms flailing up, legs pedalling.
add('Fall', 16, {
    0: pose(arm_r={'r': (-150, 0, -25)}, arm_l={'r': (-130, 0, 25)}, leg_r={'r': (-20, 0, 0)}, leg_l={'r': (15, 0, 0)}, head={'r': (10, 0, 0)}),
    8: pose(arm_r={'r': (-130, 0, -25)}, arm_l={'r': (-150, 0, 25)}, leg_r={'r': (15, 0, 0)}, leg_l={'r': (-20, 0, 0)}, head={'r': (10, 0, 0)}),
}, loop=True)
# Land: soft knee bend.
add('Land', 8, {
    0: pose(pelvis={'l': (0, 0, -2.5)}, body={'r': (14, 0, 0)}, leg_r={'r': (-20, 0, 0)}, leg_l={'r': (-20, 0, 0)}, arm_r={'r': (-20, 0, -15)}, arm_l={'r': (-20, 0, 15)}),
    8: pose(),
})
# Hard landing: heavy squat, slow recovery.
HARD = pose(pelvis={'l': (0, 0, -5.5)}, body={'r': (40, 0, 0)}, head={'r': (-25, 0, 0)}, leg_r={'r': (-45, 0, -6)}, leg_l={'r': (-45, 0, 6)}, arm_r={'r': (-10, 0, -10)}, arm_l={'r': (-10, 0, 10)})
add('HardLand', 20, {0: HARD, 12: HARD, 20: pose()})
# Ground pound: forward spin while hovering, then a tucked plummet, then the impact squat.
add('GroundPoundSpin', 10, {
    0: pose(**TUCK),
    3: pose(pelvis={'r': (110, 0, 0), 'l': (0, 0, 2)}, **TUCK),
    6: pose(pelvis={'r': (230, 0, 0), 'l': (0, 0, 2)}, **TUCK),
    10: pose(pelvis={'r': (360, 0, 0), 'l': (0, 0, 1)}, **TUCK),
})
GPF = pose(pelvis={'r': (0, 0, 0)}, body={'r': (20, 0, 0)}, head={'r': (-15, 0, 0)}, leg_r={'r': (-80, 0, 0)}, leg_l={'r': (-80, 0, 0)}, arm_r={'r': (-30, 0, 20)}, arm_l={'r': (-30, 0, -20)})
add('GroundPoundFall', 10, {0: GPF, 10: GPF})
add('GroundPoundLand', 18, {
    0: pose(pelvis={'l': (0, 0, -6)}, body={'r': (25, 0, 0)}, leg_r={'r': (-60, 0, -20)}, leg_l={'r': (-60, 0, 20)}, arm_r={'r': (0, 0, -75)}, arm_l={'r': (0, 0, 75)}),
    10: pose(pelvis={'l': (0, 0, -5)}, body={'r': (20, 0, 0)}, leg_r={'r': (-55, 0, -20)}, leg_l={'r': (-55, 0, 20)}, arm_r={'r': (0, 0, -70)}, arm_l={'r': (0, 0, 70)}),
    18: pose(),
})
# Wall cling (the kick window): pressed against the wall.
CLING = pose(body={'r': (-10, 0, 0)}, head={'r': (12, 0, 0)}, arm_r={'r': (-100, 0, -35)}, arm_l={'r': (-100, 0, 35)}, leg_r={'r': (-50, 0, 0)}, leg_l={'r': (5, 0, 0)})
add('WallCling', 6, {0: CLING, 6: CLING})
# Wall kick: pushing off, one leg extended back, arms swinging up.
add('WallKick', 18, {
    0: pose(**CLING),
    4: pose(body={'r': (-12, 0, 0)}, arm_r={'r': (-160, 0, -30)}, arm_l={'r': (-140, 0, 30)}, leg_r={'r': (-60, 0, 0)}, leg_l={'r': (50, 0, 0)}),
    18: pose(body={'r': (0, 0, 0)}, arm_r={'r': (-140, 0, -40)}, arm_l={'r': (-130, 0, 40)}, leg_r={'r': (-30, 0, 0)}, leg_l={'r': (20, 0, 0)}),
})
# Bonk: knocked back off a wall, head snapped back, limbs flung forward.
add('Bonk', 20, {
    0: pose(body={'r': (-35, 0, 0)}, head={'r': (30, 0, 0)}, arm_r={'r': (-80, 0, -20)}, arm_l={'r': (-80, 0, 20)}, leg_r={'r': (-50, 0, 0)}, leg_l={'r': (-40, 0, 0)}),
    10: pose(pelvis={'r': (-25, 0, 0)}, body={'r': (-30, 0, 0)}, head={'r': (25, 0, 0)}, arm_r={'r': (-110, 0, -40)}, arm_l={'r': (-110, 0, 40)}, leg_r={'r': (-60, 0, 0)}, leg_l={'r': (-30, 0, 0)}),
    20: pose(pelvis={'r': (-30, 0, 0)}, body={'r': (-25, 0, 0)}, head={'r': (20, 0, 0)}, arm_r={'r': (-100, 0, -50)}, arm_l={'r': (-100, 0, 50)}, leg_r={'r': (-55, 0, 0)}, leg_l={'r': (-35, 0, 0)}),
})

# ------------------------------------------------------------------ export
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
for pb in arm.pose.bones: pb.rotation_euler = (0, 0, 0); pb.location = (0, 0, 0)
export(os.path.join(fbx_dir, 'Steve.fbx'), [arm, mesh], False)
print('EXPORTED Steve.fbx')
report = {'fps': FPS, 'clips': {}}
for name, length, keys, loop in CLIPS:
    act, n = clip(name, length, keys, loop)
    scene.frame_start, scene.frame_end = 0, n
    export(os.path.join(fbx_dir, 'S_%s.fbx' % name), [arm], True)
    report['clips'][name] = {'frames': n, 'seconds': n / FPS, 'loop': loop}
    print('EXPORTED S_%s.fbx (%d frames%s)' % (name, n, ', loop' if loop else ''))

lines = sorted(('%s>%s' % (b.name, b.parent.name if b.parent else '')).lower() for b in arm.data.bones)
crc = zlib.crc32(''.join(l + '\n' for l in lines).encode('utf-8')) & 0xffffffff
json.dump({'bones': [b.name for b in arm.data.bones], 'crc': '%08X' % crc}, open(os.path.join(out, 'skeleton.json'), 'w'), indent=1)
json.dump(report, open(os.path.join(out, 'clips.json'), 'w'), indent=1)
print('SKELETON crc %08X bones %d' % (crc, len(arm.data.bones)))

# ------------------------------------------------------------------ previews (workbench, textured with the placeholder)
skin_img = bpy.data.images.get('T_SteveSkin_Default')
for m in me.materials:
    m.use_nodes = True
    nt = m.node_tree; bsdf = nt.nodes.get('Principled BSDF')
    t = nt.nodes.new('ShaderNodeTexImage'); t.image = skin_img; t.interpolation = 'Closest'
    nt.links.new(t.outputs['Color'], bsdf.inputs['Base Color'])
    if m.name == 'Overlay':
        nt.links.new(t.outputs['Alpha'], bsdf.inputs['Alpha']); m.blend_method = 'CLIP'
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'; scene.display.shading.color_type = 'TEXTURE'
scene.render.resolution_x, scene.render.resolution_y = 480, 640
scene.world = bpy.data.worlds.new('w'); scene.world.color = (0.08, 0.08, 0.1)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 3.2
def shoot(fname, ang):
    d = Vector((0, -1, 0)); d.rotate(Matrix.Rotation(math.radians(ang), 3, 'Z'))
    cam.location = Vector((0, 0, 0.95)) + d * 10.0
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(out, fname)
    bpy.ops.render.render(write_still=True)
arm.animation_data.action = None
for pb in arm.pose.bones: pb.rotation_euler = (0, 0, 0); pb.location = (0, 0, 0)
for s in ('arm_r_slim', 'arm_l_slim'): arm.pose.bones[s].scale = (0.001, 0.001, 0.001)
bpy.context.view_layer.update()
shoot('preview_rest_front.png', 0); shoot('preview_rest_side.png', 90); shoot('preview_rest_back.png', 180)
for name, frac in (('Run', 0.0), ('TripleJump', 0.4), ('Backflip', 0.5), ('Sideflip', 0.45), ('LongJump', 0.6), ('GroundPoundLand', 0.1), ('WallKick', 0.3), ('Skid', 0.5)):
    act = bpy.data.actions.get('S_' + name)
    arm.animation_data.action = act
    scene.frame_set(int(act['crb_len'] * frac))
    for s in ('arm_r_slim', 'arm_l_slim'): arm.pose.bones[s].scale = (0.001, 0.001, 0.001)
    shoot('pose_%s.png' % name, 60)
print('DONE')
