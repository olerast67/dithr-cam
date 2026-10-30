"""Синтетическая сцена для витрины стилей: без чужих фотографий.

Закатное небо, солнце, три гряды гор, ельник, озеро с отражением и рябью,
песчаная коса и идущий человек. Кадр k двигает человека, поэтому GHOST и
TIDE видят настоящее движение.

    from gen_scene import scene; img = scene(walk=0.3)   # PIL.Image 1600x1200
"""
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 1600, 1200
HORIZON = 640


def ridge(seed, base, amp, rough):
    rng = np.random.default_rng(seed)
    x = np.arange(W, dtype=np.float32)
    y = np.full(W, float(base))
    for octave in range(6):
        f = (2 ** octave) / W * rough
        y += amp / (2.2 ** octave) * np.sin(2 * math.pi * f * x + rng.uniform(0, 6.28))
    return y


def background(seed=3):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    img = np.zeros((H, W, 3), np.float32)
    # небо: индиго -> пурпур -> оранжевый у горизонта
    t = np.clip(yy / HORIZON, 0, 1)[..., None]
    top, mid, low = np.array([24, 18, 70]), np.array([150, 50, 140]), np.array([255, 150, 90])
    sky = np.where(t < 0.6, top + (mid - top) * (t / 0.6), mid + (low - mid) * ((t - 0.6) / 0.4))
    img[:] = sky
    # облака: мягкие полосы
    rng = np.random.default_rng(seed)
    for _ in range(9):
        cy, cx = rng.uniform(80, 480), rng.uniform(0, W)
        w_, h_ = rng.uniform(200, 520), rng.uniform(14, 40)
        d = ((xx - cx) / w_) ** 2 + ((yy - cy) / h_) ** 2
        img += (np.exp(-d * 2)[..., None]) * np.array([255, 190, 200]) * 0.35
    # звёзды вверху
    for _ in range(260):
        x, y = int(rng.uniform(0, W)), int(rng.uniform(0, 220))
        img[y, x] += 120 * (1 - y / 220)
    # солнце и ореол
    sx, sy = 1060, 545
    d = np.sqrt((xx - sx) ** 2 + (yy - sy) ** 2)
    img += (np.exp(-d / 160)[..., None]) * np.array([255, 170, 110]) * 0.9
    img = np.where((d < 95)[..., None], np.array([255, 236, 190]), img)
    # три гряды гор, ближние темнее
    layers = [(ridge(1, 470, 70, 3), np.array([120, 70, 140]), 0.55),
              (ridge(2, 540, 80, 5), np.array([70, 40, 100]), 0.3),
              (ridge(4, 600, 45, 9), np.array([30, 22, 54]), 0.1)]
    for line, col, haze in layers:
        mask = yy >= line[None, :]
        shade = col * (1 - haze) + np.array([255, 150, 120]) * haze * 0.5
        img = np.where(mask[..., None], shade, img)
    # ельник у подножия ближней гряды
    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(pil)
    for i in range(140):
        x = rng.uniform(0, W)
        hgt = rng.uniform(30, 90)
        base = HORIZON - rng.uniform(0, 12)
        dr.polygon([(x, base - hgt), (x - hgt * 0.22, base), (x + hgt * 0.22, base)], fill=(16, 18, 34))
    img = np.asarray(pil).astype(np.float32)
    # озеро: отражение верхней части с рябью
    for y in range(HORIZON, H):
        src = HORIZON - (y - HORIZON) - 1
        src = max(src, 0)
        shift = int(6 * math.sin(y * 0.35) + 4 * math.sin(y * 0.11))
        row = np.roll(img[src], shift, axis=0)
        k = 0.62 - (y - HORIZON) / (H - HORIZON) * 0.25
        img[y] = row * k + np.array([20, 30, 70]) * (1 - k)
    # блики на воде под солнцем
    for _ in range(500):
        y = int(rng.uniform(HORIZON + 4, H))
        x = int(sx + rng.normal(0, 40 + (y - HORIZON) * 0.25))
        ln = int(rng.uniform(6, 40))
        if 0 <= x < W - ln:
            img[y, x:x + ln] = np.minimum(img[y, x:x + ln] + 90, 255)
    # песчаная коса слева внизу
    coast = ridge(7, 900, 40, 2)
    for x in range(0, W):
        top_y = int(coast[x] + (x / W) * 260)
        if top_y < H:
            img[top_y:, x] = np.array([60, 40, 50]) + rng.normal(0, 6, (H - top_y, 3))
    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
    return pil, coast


def person(dr, x, ground, phase, scale=1.0):
    """Силуэт идущего человека (шаг по фазе 0..1)."""
    s = 90 * scale
    hip = (x, ground - s * 0.95)
    neck = (x + s * 0.04, ground - s * 1.6)
    col = (8, 6, 14)
    a = math.sin(phase * 2 * math.pi)
    for sign in (1, -1):  # ноги
        knee = (hip[0] + sign * a * s * 0.22, hip[1] + s * 0.45)
        foot = (knee[0] + sign * a * s * 0.12 - s * 0.05, ground)
        dr.line([hip, knee, foot], fill=col, width=int(s * 0.12), joint="curve")
    dr.line([hip, neck], fill=col, width=int(s * 0.2))
    for sign in (1, -1):  # руки
        el = (neck[0] - sign * a * s * 0.18, neck[1] + s * 0.32)
        hand = (el[0] - sign * a * s * 0.1, el[1] + s * 0.3)
        dr.line([(neck[0], neck[1] + s * 0.05), el, hand], fill=col, width=int(s * 0.09), joint="curve")
    r = s * 0.13
    dr.ellipse([neck[0] - r, neck[1] - r * 2.1, neck[0] + r, neck[1] - 0.1 * r], fill=col)


_BG = None


def scene(walk=0.3):
    """walk 0..1: где человек на косе. Возвращает PIL RGB 1600x1200."""
    global _BG
    if _BG is None:
        _BG = background()
    bg, coast = _BG
    img = bg.copy()
    dr = ImageDraw.Draw(img)
    x = 140 + walk * 900
    ground = coast[int(x)] + (x / W) * 260 + 6
    person(dr, x, ground, phase=walk * 6, scale=1.3)
    # отражение человека в мокром песке: полупрозрачное
    refl = Image.new("RGBA", img.size, (0, 0, 0, 0))
    rd = ImageDraw.Draw(refl)
    person(rd, x, ground, phase=walk * 6, scale=1.3)
    refl = refl.transpose(Image.FLIP_TOP_BOTTOM)
    off = int(2 * ground - H)
    refl = refl.transform(img.size, Image.AFFINE, (1, 0, 0, 0, 1, -off))
    a = refl.split()[3].point(lambda v: v * 0.35)
    img.paste(refl.convert("RGB"), (0, 0), a)
    return img


if __name__ == "__main__":
    scene(0.35).save("scene_test.jpg", quality=92)
    print("ok")
