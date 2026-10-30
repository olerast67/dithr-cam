"""Пронумерованный каталог вариантов (по готовым картинкам host/out).

    python host/variants_sheet.py            новые стили, номера с 35
    python host/variants_sheet.py --old      оставшиеся старые варианты

Каждая плитка: снимок на трёх фото и как стиль выглядит в видоискателе.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "out"
NEW = [
    ("TIDE", ["SLIT", "RADIAL", "LIQUID"]),
    ("PULSE", ["SCOPE", "PAPER", "CHROMA"]),
    ("FLOW", ["STROKE", "SMOKE", "FUR"]),
    ("SHARD", ["GLASS", "MOSAIC", "PRISM"]),
    ("HOLO", ["CLOUD", "WIRE", "SCAN"]),
    ("LOOP", ["SPIRAL", "TUNNEL", "VORTEX"]),
    ("TOPO", ["MAP", "NIGHT"]),
]
PHOTOS = [1, 0, 3]  # пляж с человеком, фьорд, тюльпаны
TW, TH = 400, 300
BAR = 64


def font(size):
    for f in ("C:/Windows/Fonts/consolab.ttf", "C:/Windows/Fonts/arialbd.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def tile(num, style, vi, vname, with_prev):
    cols = len(PHOTOS) + (1 if with_prev else 0)
    t = Image.new("RGB", (TW * cols, TH + BAR), (14, 12, 20))
    for k, p in enumerate(PHOTOS):
        im = Image.open(OUT / f"p{p}_{style}_{vi}.png").convert("RGB").resize((TW, TH), Image.LANCZOS)
        t.paste(im, (k * TW, BAR))
    d = ImageDraw.Draw(t)
    if with_prev:
        pv = Image.open(OUT / f"p1_{style}_prev.png").convert("RGB")
        pv = pv.resize((TW - 40, (TW - 40) * 3 // 4), Image.NEAREST)
        t.paste(pv, (len(PHOTOS) * TW + 20, BAR + (TH - pv.height) // 2))
        d.text((len(PHOTOS) * TW + 24, BAR + 6), "OLED", font=font(22), fill=(150, 150, 160))
    d.rectangle((0, 0, 110, BAR - 1), fill=(0, 200, 255))
    d.text((14, 6), f"{num}", font=font(48), fill=(0, 0, 0))
    d.text((130, 12), f"{style} / {vname}", font=font(36), fill=(240, 240, 240))
    return t


def main():
    tiles, num = [], 35
    for style, vars_ in NEW:
        for vi, vn in enumerate(vars_):
            tiles.append(tile(num, style, vi, vn, with_prev=(vi == 0)))
            num += 1
    cols = 2
    tw = max(t.width for t in tiles)
    th = tiles[0].height
    g = 12
    for si, part in enumerate((tiles[:10], tiles[10:]), 1):
        rows = (len(part) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * tw + (cols + 1) * g, rows * th + (rows + 1) * g), (0, 0, 0))
        for i, t in enumerate(part):
            sheet.paste(t, (g + (i % cols) * (tw + g), g + (i // cols) * (th + g)))
        sheet = sheet.resize((sheet.width * 3 // 5, sheet.height * 3 // 5), Image.LANCZOS)
        sheet.save(OUT / f"new_{si}.jpg", quality=86)
        print(OUT / f"new_{si}.jpg", sheet.size)


if __name__ == "__main__":
    sys.exit(main())
