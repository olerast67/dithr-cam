"""Сборка darkroom и прогон стилей на тестовых фото.

    python host/run.py                      все стили, вариант по seed
    python host/run.py --style NEON         все варианты одного стиля
    python host/run.py --style NEON a.jpg   на своих фото

Компилятор C: по умолчанию Zig (pip install ziglang), 32-битная сборка как
у ESP32. Результат: host/out/<стиль>.jpg — лист: строки фото, столбцы варианты.
"""
import argparse
import shlex
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
FW = HERE.parent / "firmware"
FX = FW / "components" / "fx"
OUT = HERE / "out"
VARIANTS = {
    "NEON": ["VAPOR", "MANGA"],
    "Y2K": ["ICE", "CHROME", "SUNSET", "TOXIC"],
    "MELT": ["DRIP", "RISE", "WIND", "MOLTEN"],
    "GHOST": ["RGB", "ECHO", "XRAY"],
    "MOSH": ["CRPT", "BLOCK"],
    "DMG": ["DMG", "POCKET", "SGB", "PRINT"],
    "INK": ["ENGRV", "RISO"],
    "TIDE": ["SLIT", "RADIAL", "LIQUID"],
    "PULSE": ["SCOPE", "CHROMA"],
    "SHARD": ["GLASS", "MOSAIC", "PRISM"],
    "HOLO": ["CLOUD", "WIRE", "SCAN"],
    "LOOP": ["SPIRAL", "VORTEX"],
    "TOPO": ["MAP"],
}
STYLES = list(VARIANTS)
DEFAULT = [
    "C:/Windows/Web/Screen/img103.png",
    "C:/Windows/Web/Wallpaper/Theme1/img1.jpg",
    "C:/Windows/Web/Wallpaper/Theme1/img2.jpg",
    "C:/Windows/Web/Wallpaper/Theme2/img10.jpg",
]
DEFAULT_CC = "python -m ziglang cc -target x86-windows-gnu"


def build(cc):
    exe = HERE / "darkroom.exe"
    srcs = [HERE / "darkroom.c", FW / "main" / "codec.c", FW / "components" / "jpegdec" / "jpegdec.c"]
    srcs += sorted(FX.glob("*.c"))
    cmd = shlex.split(cc) + ["-O2", "-std=gnu11", "-Wall", "-Wno-unused-parameter", "-Wno-unused-function",
                             "-D__LINUX__", "-I", str(HERE / "stubs"), "-I", str(FW / "main"),
                             "-I", str(FX / "include"), "-I", str(FX), "-I", str(FW / "components" / "jpegdec"),
                             "-o", str(exe)] + [str(s) for s in srcs]
    subprocess.run(cmd, check=True)
    return exe


def crop43(im, dx=0, dy=0, zoom=1.0):
    w, h = im.size
    tw = int(min(w, h * 4 // 3) * zoom)
    th = tw * 3 // 4
    x0 = min(max((w - tw) // 2 + dx, 0), w - tw)
    y0 = min(max((h - th) // 2 + dy, 0), h - th)
    return im.crop((x0, y0, x0 + tw, y0 + th))


def prepare(src, base):
    """Как камера: JPEG 1600x1200 4:2:2, превью 320x240 и три кадра для GHOST."""
    im = Image.open(src).convert("RGB")
    cap = base.with_name(base.name + "_cap.jpg")
    crop43(im, zoom=0.92).resize((1600, 1200), Image.LANCZOS).save(cap, quality=90, subsampling=1)
    crop43(im, zoom=0.92).resize((320, 240), Image.LANCZOS).save(base.with_name(base.name + "_qvga.jpg"),
                                                                 quality=80, subsampling=1)
    frames = [str(cap)]
    for k in (1, 2):
        # Камера чуть дрогнула, а «объект» в центре кадра сдвинулся заметно.
        fr = crop43(im, dx=3 * k, dy=-k, zoom=0.92).resize((1600, 1200), Image.LANCZOS)
        box = (560, 300, 1120, 1000)
        obj = fr.crop(box)
        fr.paste(obj, (box[0] + 70 * k, box[1] - 10 * k))
        f = base.with_name(base.name + f"_f{k}.jpg")
        fr.save(f, quality=90, subsampling=1)
        frames.append(str(f))
    # TIDE: 24 кадра 800x600, камера плавно ведёт, «объект» проезжает через кадр
    tide = []
    for k in range(24):
        fr = crop43(im, dx=k * 6, dy=int(4 * __import__("math").sin(k / 3)), zoom=0.85).resize((800, 600), Image.LANCZOS)
        box = (260, 150, 560, 520)
        obj = fr.crop(box)
        fr.paste(obj, (box[0] + (k - 12) * 14, box[1] + int(30 * __import__("math").sin(k / 4))))
        f = base.with_name(base.name + f"_t{k:02d}.jpg")
        fr.save(f, quality=88, subsampling=1)
        tide.append(str(f))
    return frames, tide


def render(exe, style, var, seed, frames, out, tide=None):
    src = frames if style == "GHOST" else (tide if style == "TIDE" else frames[:1])
    args = [str(exe), style, str(var), str(seed), str(out)] + src
    r = subprocess.run(args, check=True, capture_output=True, text=True)
    return r.stdout.strip()


def label(im, text):
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 8 * len(text) + 8, 18), fill=(0, 0, 0))
    d.text((4, 3), text, fill=(255, 255, 255))
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photos", nargs="*")
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--style")
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    OUT.mkdir(exist_ok=True)
    exe = build(a.cc)
    photos = a.photos or DEFAULT
    styles = [a.style] if a.style else STYLES
    for st in styles:
        cols = list(range(len(VARIANTS[st]))) if a.style else [-1]
        rows = []
        for i, ph in enumerate(photos):
            frames, tide = prepare(ph, OUT / f"p{i}")
            row = [label(Image.open(frames[0]).convert("RGB").resize((800, 600)), "SRC")]
            for v in cols:
                ext = ".png" if st in ("DMG", "INK") else ".ppm"
                out = OUT / f"p{i}_{st}_{v}{ext}"
                info = render(exe, st, v, a.seed + i, frames, out, tide)
                print(info)
                im = Image.open(out).convert("RGB")
                full = OUT / f"p{i}_{st}_{v}.png"
                im.save(full)
                if ext == ".ppm":
                    out.unlink()
                row.append(label(im.resize((800, 600), Image.NEAREST if st == "DMG" else Image.LANCZOS),
                                 info.split()[0]))
            rows.append(row)
        tw, th = 480, 360
        sheet = Image.new("RGB", (tw * len(rows[0]), th * len(rows)))
        for r, row in enumerate(rows):
            for c, im in enumerate(row):
                sheet.paste(im.resize((tw, th), Image.LANCZOS), (c * tw, r * th))
        sheet.save(OUT / f"{st}.jpg", quality=88)
        print("sheet", OUT / f"{st}.jpg")


if __name__ == "__main__":
    sys.exit(main())
