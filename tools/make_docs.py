"""Иллюстрации для README из синтетической сцены (без чужих фото).

    python tools/make_docs.py [--cc "python -m ziglang cc -target x86-windows-gnu"]

Делает:
  docs/img/<СТИЛЬ>-<ВАРИАНТ>.jpg  снимок каждого варианта тем же путём, что в камере
  docs/img/source.jpg             исходная сцена
  docs/img/hero.jpg               баннер из восьми стилей
  docs/img/oled.gif               экран камеры: видоискатель, переключение стилей, снимок
"""
import argparse
import math
import shlex
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "host"))
from gen_scene import scene  # noqa: E402
import run as host_run  # noqa: E402

TMP = ROOT / "host" / "out" / "docs_tmp"
IMG = ROOT / "docs" / "img"


def frames():
    TMP.mkdir(parents=True, exist_ok=True)
    cap = TMP / "cap.jpg"
    scene(0.35).save(cap, quality=90, subsampling=1)
    ghost = [str(cap)]
    for k in (1, 2):
        f = TMP / f"g{k}.jpg"
        scene(0.35 + 0.035 * k).save(f, quality=90, subsampling=1)
        ghost.append(str(f))
    tide = []
    for k in range(24):
        f = TMP / f"t{k:02d}.jpg"
        scene(0.15 + k * 0.022).resize((800, 600), Image.LANCZOS).save(f, quality=88, subsampling=1)
        tide.append(str(f))
    scene(0.35).resize((800, 600), Image.LANCZOS).save(IMG / "source.jpg", quality=86)
    return ghost, tide


def shots(exe, ghost, tide):
    for st, vars_ in host_run.VARIANTS.items():
        for vi, vn in enumerate(vars_):
            ext = ".png" if st in ("DMG", "INK") else ".ppm"
            out = TMP / f"{st}_{vi}{ext}"
            src = ghost if st == "GHOST" else (tide if st == "TIDE" else ghost[:1])
            subprocess.run([str(exe), st, str(vi), "11", str(out)] + src, check=True, capture_output=True)
            im = Image.open(out).convert("RGB")
            if st == "DMG":  # пиксель-арт без размытия
                im = im.resize((im.width // 5 * 4, im.height // 5 * 4), Image.NEAREST)
            else:
                im = im.resize((640, 480), Image.LANCZOS)
            im.save(IMG / f"{st}-{vn}.jpg", quality=86)
            out.unlink()
            print(st, vn)


def hero():
    picks = ["LOOP-SPIRAL", "PULSE-CHROMA", "NEON-VAPOR", "SHARD-GLASS", "Y2K-ICE", "HOLO-CLOUD", "TOPO-MAP", "GHOST-ECHO"]
    tw, th = 400, 300
    banner = Image.new("RGB", (tw * 4, th * 2), (0, 0, 0))
    for i, name in enumerate(picks):
        im = Image.open(IMG / f"{name}.jpg").convert("RGB").resize((tw, th), Image.LANCZOS)
        banner.paste(im, ((i % 4) * tw, (i // 4) * th))
    banner.save(IMG / "hero.jpg", quality=85)


def oled_gif(cc):
    exe = ROOT / "host" / "oled_sim.exe"
    fx = ROOT / "firmware" / "components" / "fx"
    srcs = [ROOT / "host" / "oled_sim.c", ROOT / "firmware" / "main" / "ui.c"] + sorted(fx.glob("*.c"))
    subprocess.run(shlex.split(cc) + ["-O2", "-w", "-I", str(ROOT / "host" / "stubs"), "-I", str(ROOT / "firmware" / "main"),
                                      "-I", str(fx / "include"), "-I", str(fx), "-o", str(exe)] + [str(s) for s in srcs],
                   check=True)
    # «Камера» в руке: медленная панорама по сцене, человек идёт
    with open(TMP / "frames.bin", "wb") as f:
        for i in range(520):
            t = i / 520
            im = scene(0.1 + t * 0.8)
            cw = int(1600 * 0.62)
            ch = cw * 3 // 4
            x0 = int(300 * (0.5 + 0.5 * math.sin(t * 6.28)) + 8 * math.sin(i * 0.7))
            y0 = int(300 + 6 * math.cos(i * 0.6))
            fr = im.crop((x0, y0, x0 + cw, y0 + ch)).resize((160, 120), Image.BOX).convert("L")
            f.write(fr.tobytes())
    scene(0.5).resize((800, 600), Image.LANCZOS).save(TMP / "cap.ppm")
    subprocess.run([str(exe), str(TMP / "frames.bin"), str(TMP / "cap.ppm"), str(TMP / "screen.bin")], check=True, cwd=TMP)
    data = (TMP / "screen.bin").read_bytes()
    rec = 2 + 128 * 128
    out, durs = [], []
    on, off, gap = (232, 240, 255), (10, 10, 14), (4, 4, 6)
    for i in range(0, len(data), rec):
        (d,) = struct.unpack("<H", data[i:i + 2])
        buf = data[i + 2:i + rec]
        im = Image.new("RGB", (128 * 3, 128 * 3), gap)
        px = im.load()
        for y in range(128):
            for x in range(128):
                c = on if buf[y * 128 + x] else off
                px[x * 3, y * 3] = c
                px[x * 3 + 1, y * 3] = c
                px[x * 3, y * 3 + 1] = c
                px[x * 3 + 1, y * 3 + 1] = c
        out.append(im)
        durs.append(max(d, 20))
    out[0].save(IMG / "oled.gif", save_all=True, append_images=out[1:], duration=durs, loop=0, optimize=True)
    print("oled.gif", len(out), "frames")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default=host_run.DEFAULT_CC)
    a = ap.parse_args()
    IMG.mkdir(parents=True, exist_ok=True)
    exe = host_run.build(a.cc)
    ghost, tide = frames()
    shots(exe, ghost, tide)
    hero()
    oled_gif(a.cc)


if __name__ == "__main__":
    main()
