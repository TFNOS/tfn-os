# FOUR SEASONS: the colours of the four seasons (the palettes of the look page g_seasons.js,
# with more materials), the light of each season (the place of the sun, the light direction, the length of a shadow),
# and the ramps of the materials (dark to light, with a hue shift: the shadows go to the cool tint of the season, the
# lit parts to the colour of its sun).
from fsart import hx, mix, mul, clamp

SPRING, SUMMER, AUTUMN, WINTER = 0, 1, 2, 3
NAMES = ['spring', 'summer', 'autumn', 'winter']

LOOK = [
    dict(skyT='#64b0ea', skyB='#e2f5f0', sun='#fff8dc', sunG='#fff2bf', hz='#f4f2d9', cl='#ffffff', clS='#bcd3e6', clH='#ffffff',
         mtn='#8fb2c9', cap='#ffffff', ridge='#7fb592', farT='#5c9a63',
         hillL='#b0e47a', hill='#8ccd5e', hillS='#5aa04e', grass='#4a8a44', grassL='#dcf7a6',
         lfL='#cdf18f', lf='#93d364', lfD='#5b9c48', vine='#a8dc70', soil='#8f744d', bush='#ef8fb6', bushL='#ffd0e2',
         wood='#7a5536', woodD='#4f3522', path='#dcc391', water='#8ccfe0', waterD='#4f9dc4',
         shed='#c4473a', shedD='#8e2f2a', roof='#6f6c78', trim='#f3ebe0', shadow='#3b7646', fg='#4e9a45'),
    dict(skyT='#1d72d6', skyB='#9ad8f7', sun='#ffffff', sunG='#fffbd2', hz='#e8f6f4', cl='#ffffff', clS='#adc9e6', clH='#ffffff',
         mtn='#6f9cbb', cap='#e6f0f5', ridge='#5d9b70', farT='#3d7c4a',
         hillL='#74c246', hill='#4c9a36', hillS='#2f7230', grass='#245e28', grassL='#9ade66',
         lfL='#7fcc4e', lf='#46963b', lfD='#2a6b2d', vine='#55a33f', soil='#8a6a42', bush='#3f8f37', bushL='#7fcc4e',
         wood='#74502f', woodD='#4a311e', path='#d6b97c', water='#3fa2d6', waterD='#2870a8',
         shed='#c84a3c', shedD='#902f2a', roof='#64616c', trim='#f6efe4', shadow='#2a5f2f', fg='#3b8a3a'),
    dict(skyT='#5f7fbf', skyB='#fbcf90', sun='#ffda96', sunG='#ffb460', hz='#ffd7a2', cl='#ffcfb2', clS='#a87488', clH='#fff0d6',
         mtn='#9a7c9b', cap='#f6e4e6', ridge='#a97a50', farT='#cf6a2f',
         hillL='#dcbf5b', hill='#ae943c', hillS='#796633', grass='#6a5a2b', grassL='#f4d67e',
         lfL='#ffc54c', lf='#f2812d', lfD='#b8431f', vine='#db5c2c', soil='#7a5a36', bush='#c9402a', bushL='#f07a3a',
         wood='#6f4a2c', woodD='#47301c', path='#c9a46a', water='#7ea6bf', waterD='#4c6f89',
         shed='#b9423a', shedD='#842d29', roof='#5f5a63', trim='#efe2cf', shadow='#5a4a2b', fg='#8a7334'),
    dict(skyT='#86a2c8', skyB='#edf1f7', sun='#fff4ec', sunG='#ffdcd2', hz='#f6e5e6', cl='#edf0f6', clS='#a4b2c8', clH='#ffffff',
         mtn='#b5c5db', cap='#ffffff', ridge='#c8d3e3', farT='#8e9db5',
         hillL='#f7fafd', hill='#e2e9f2', hillS='#a7b8d6', grass='#9aaccc', grassL='#ffffff',
         lfL='#f6f9fc', lf='#e2e9f2', lfD='#a8b8d6', vine='#e6ecf4', soil='#e0e7f0', bush='#e9eef6', bushL='#ffffff',
         wood='#5e4433', woodD='#3b2a20', path='#e3eaf3', water='#d6eaf4', waterD='#a9cbe0',
         shed='#ad3d36', shedD='#782a27', roof='#eef3f8', trim='#f4efe8', shadow='#94a6c8', fg='#d8e1ee'),
]

# the sun in the sky of a wide view (x, y in the 640 x 172 frame of the look page) and the light in 3D (to the light:
# x right, y down, z to the eye); the ground shadow: its slant (px to the side for each px of height) and length
SUN = [(118, 30), (318, 12), (522, 46), (596, 64)]
LIGHT = [(-0.62, -0.55, 0.56), (-0.18, -0.86, 0.48), (0.66, -0.42, 0.62), (0.84, -0.22, 0.50)]
SLANT = [0.9, 0.25, -1.1, -2.1]          # a shadow goes to the right in spring, down in summer, left and long in winter
SHLEN = [0.30, 0.18, 0.42, 0.62]         # the length of a ground shadow for each px of height (a flat ellipse)

# the cool tint of the shadows and the colour of the light of each season (the hue shift of the ramps)
COOL = ['#3a5a8a', '#28405e', '#4a3360', '#4a5f8f']
WARM = ['#fff6c8', '#fff4b0', '#ffc070', '#ffe8de']
# the light of a window in a room: its colour and its strength
WIN = ['#f2f6d8', '#ffe9a8', '#ffb868', '#cfe0ff']


class Pal(dict):
    __getattr__ = dict.__getitem__


def shade(c, k, s):
    """the colour c in the light k: k < 0 to the cool shadow tint of the season, k > 0 to its warm light"""
    if k < 0:
        return mix(mul(c, 1 + k * 0.55), hx(COOL[s]), -k * 0.35)
    return mix(c, hx(WARM[s]), k * 0.55)


def ramp_of(c, s, n=4, lo=-0.75, hi=0.55):
    return [shade(c, lo + (hi - lo) * i / (n - 1), s) for i in range(n)]


def pal(s):
    L = LOOK[s]
    p = Pal({k: hx(v) for k, v in L.items()})
    p.s = s
    p.sun_xy = SUN[s]
    p.L = LIGHT[s]
    p.slant = SLANT[s]
    p.shlen = SHLEN[s]
    p.cool = hx(COOL[s])
    p.warm = hx(WARM[s])
    p.win = hx(WIN[s])
    w = s == WINTER
    # the ramps (dark -> light)
    p.leaf = [p.lfD, mix(p.lfD, p.lf, 0.55), p.lf, p.lfL, mix(p.lfL, (255, 255, 255), 0.4)]
    p.grassr = [p.grass, p.hillS, p.hill, p.hillL, p.grassL]
    p.bark = [mul(p.woodD, 0.72), p.woodD, p.wood, mix(p.wood, p.warm, 0.32)]
    p.woodr = [mul(p.woodD, 0.8), p.woodD, p.wood, mix(p.wood, hx('#d8a878'), 0.45), mix(p.wood, p.warm, 0.6)]
    p.redr = [mul(p.shedD, 0.75), p.shedD, p.shed, mix(p.shed, p.warm, 0.35)]
    p.roofr = [mul(p.roof, 0.7), mul(p.roof, 0.86), p.roof, mix(p.roof, (255, 255, 255), 0.25)]
    stone = hx('#9a968e') if not w else hx('#a7acb8')
    p.stone = ramp_of(stone, s, 5, -0.85, 0.6)
    p.stoneW = ramp_of(hx('#d9cdb4'), s, 4, -0.7, 0.5)            # a light house stone (lime)
    p.brick = ramp_of(hx('#b4573f'), s, 4, -0.7, 0.45)
    p.tile = ramp_of(hx('#c4553a'), s, 4, -0.7, 0.45)              # roof tiles
    p.metal = ramp_of(hx('#7d8790'), s, 4, -0.8, 0.7)
    p.iron = ramp_of(hx('#4a4c52'), s, 4, -0.7, 0.6)
    p.rope = ramp_of(hx('#cfb985'), s, 3, -0.6, 0.35)
    p.tire = [hx('#141418'), hx('#25252b'), hx('#3a3a42'), hx('#5a5a64')]
    p.snow = [hx('#9eb0d2'), hx('#c6d3e8'), hx('#e4ecf6'), hx('#ffffff')]
    p.ice = [hx('#7fa8c8'), hx('#a9cbe0'), hx('#cfe4f0'), hx('#eef8fc')]
    p.waterr = [mul(p.waterD, 0.8), p.waterD, mix(p.waterD, p.water, 0.5), p.water, mix(p.water, (255, 255, 255), 0.5)]
    p.soilr = ramp_of(hx('#8f744d') if not w else hx('#6a5a48'), s, 4, -0.7, 0.4)
    p.mud = ramp_of(hx('#6e5638'), s, 4, -0.7, 0.35)
    p.pathr = [mul(p.path, 0.78), mul(p.path, 0.9), p.path, mix(p.path, (255, 255, 255), 0.3)]
    p.sky = [p.skyT, p.skyB, p.hz]
    p.cloud = [p.clS, mix(p.clS, p.cl, 0.5), p.cl, p.clH]
    p.blossom = [hx('#e98ab4'), hx('#f6b3d0'), hx('#ffd6e8'), hx('#ffffff')]
    p.apple = [hx('#8c1f1a'), hx('#c8352a'), hx('#e85a3c'), hx('#ffb08a')]
    p.honey = [hx('#8a4a10'), hx('#c8781c'), hx('#f0b030'), hx('#ffe080')]
    p.straw = ramp_of(hx('#d8b860'), s, 4, -0.6, 0.45)
    p.paint_blue = ramp_of(hx('#5f8fc0'), s, 4, -0.7, 0.45)
    p.paint_yel = ramp_of(hx('#e8c050'), s, 4, -0.7, 0.45)
    p.paint_white = ramp_of(hx('#e8e4da'), s, 4, -0.6, 0.35)
    p.paint_green = ramp_of(hx('#4f8a5a'), s, 4, -0.7, 0.45)
    p.plaster = ramp_of(hx('#efe6d4'), s, 4, -0.65, 0.35)
    p.glass = [hx('#2c3a4a'), hx('#47607a'), hx('#7fa0bc'), hx('#cfe2f0')]
    p.flower = {SPRING: ['#ff9ec8', '#ffe36b', '#ffffff', '#c7a4ff'], SUMMER: ['#ffffff', '#ff4a3d', '#ffe14d', '#ff9a3d'],
                AUTUMN: ['#e8582a', '#c9351f', '#ffb23d', '#a3542a'], WINTER: ['#ffffff', '#ffffff', '#ffffff', '#ffffff']}[s]
    p.flower = [hx(c) for c in p.flower]
    p.outline = mix(p.woodD, p.cool, 0.35)
    return p
