"""Схема подключения DITHR//CAM в SVG: docs/wiring.svg.

    python tools/gen_wiring.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "docs" / "wiring.svg"
FONT = "ui-monospace, SFMono-Regular, Consolas, 'Liberation Mono', monospace"

BG, PCB, PCB_EDGE = "#0e0f17", "#16261f", "#2c4a3b"
GOLD, TXT, DIM = "#d9b25a", "#e8eaf2", "#8a8fa6"
RED, GREY, BLUE, YEL, GRN, VIO = "#ff5a5f", "#c3c7d6", "#4ea8ff", "#ffd23f", "#3ee08a", "#b48cff"

# ESP32-CAM: штыри в два ряда, порядок как на шелкографии (от края с 5V/3V3)
LEFT = ["5V", "GND", "IO12", "IO13", "IO15", "IO14", "IO2", "IO4"]
RIGHT = ["3V3", "IO16", "IO0", "GND", "VCC", "U0R", "U0T", "GND"]
BX, BY, BW, BH = 120, 96, 300, 560
LX, RX = BX + 30, BX + BW - 30
PY0, PSTEP = BY + 230, 40


def pin(side, name):
    col = LEFT if side == "L" else RIGHT
    return (LX if side == "L" else RX, PY0 + col.index(name) * PSTEP)


# Экран SH1107, штыри снизу слева направо как на модуле: SDA SCL GND VCC
OX, OY, OW, OH = 760, 70, 330, 280
OPIN = {n: (OX + 60 + i * 70, OY + OH) for i, n in enumerate(["SDA", "SCL", "GND", "VCC"])}
# Кнопки
B1 = (560, 470)
B2 = (720, 470)


def wire(d, color, dash=False, w=5):
    da = ' stroke-dasharray="10 9"' if dash else ""
    return (f'<path d="{d}" fill="none" stroke="#05060a" stroke-width="{w + 5}" stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"{da}/>')


def rpath(pts, r=22):
    """Ломаная со скруглёнными углами."""
    d = f"M{pts[0][0]},{pts[0][1]}"
    for i in range(1, len(pts) - 1):
        (x0, y0), (x1, y1), (x2, y2) = pts[i - 1], pts[i], pts[i + 1]
        def towards(ax, ay, bx, by, k):
            dx, dy = bx - ax, by - ay
            L = max(abs(dx) + abs(dy), 1)
            k = min(k, L / 2)
            return ax + dx / L * k, ay + dy / L * k
        p = towards(x1, y1, x0, y0, r)
        q = towards(x1, y1, x2, y2, r)
        d += f" L{p[0]:.0f},{p[1]:.0f} Q{x1},{y1} {q[0]:.0f},{q[1]:.0f}"
    d += f" L{pts[-1][0]},{pts[-1][1]}"
    return d


def text(x, y, s, size=16, color=TXT, anchor="start", weight="400"):
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" fill="{color}" '
            f'text-anchor="{anchor}" font-weight="{weight}">{s}</text>')


def main():
    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 860" width="1200" height="860">')
    o.append(f'<rect width="1200" height="860" rx="22" fill="{BG}"/>')

    # ---- ESP32-CAM
    o.append(f'<rect x="{BX}" y="{BY}" width="{BW}" height="{BH}" rx="16" fill="{PCB}" stroke="{PCB_EDGE}" stroke-width="3"/>')
    o.append(text(BX + BW / 2, BY + 38, "ESP32-CAM", 22, TXT, "middle", "700"))
    # камера и шлейф
    o.append(f'<rect x="{BX + BW / 2 - 46}" y="{BY + 58}" width="92" height="92" rx="10" fill="#0b0c12" stroke="#3a3f55" stroke-width="2"/>')
    o.append(f'<circle cx="{BX + BW / 2}" cy="{BY + 104}" r="30" fill="#14161f" stroke="#4a5070" stroke-width="3"/>')
    o.append(f'<circle cx="{BX + BW / 2}" cy="{BY + 104}" r="13" fill="#2b3a6a"/>')
    o.append(text(BX + BW / 2, BY + 172, "OV2640", 14, DIM, "middle"))
    # слот microSD
    o.append(f'<rect x="{BX + BW / 2 - 50}" y="{BY + 190}" width="100" height="22" rx="4" fill="#8a8f9e" opacity="0.55"/>')
    o.append(text(BX + BW / 2, BY + 230, "microSD", 13, DIM, "middle"))
    for side, col in (("L", LEFT), ("R", RIGHT)):
        for n in col:
            x, y = pin(side, n)
            o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="{GOLD}" stroke="#6b5321" stroke-width="2"/>')
            o.append(text(x + (18 if side == "L" else -18), y + 5, n, 15, TXT, "start" if side == "L" else "end"))
    # программатор MB
    o.append(f'<rect x="{BX + 20}" y="{BY + BH + 14}" width="{BW - 40}" height="48" rx="10" fill="none" stroke="#3a3f55" stroke-width="2" stroke-dasharray="6 6"/>')
    o.append(text(BX + BW / 2, BY + BH + 44, "ESP32-CAM-MB · micro-USB", 14, DIM, "middle"))

    # ---- экран
    o.append(f'<rect x="{OX}" y="{OY}" width="{OW}" height="{OH}" rx="14" fill="#141724" stroke="#3a3f5a" stroke-width="3"/>')
    o.append(f'<rect x="{OX + 45}" y="{OY + 30}" width="{OW - 90}" height="{OH - 110}" rx="6" fill="#020203"/>')
    o.append(text(OX + OW / 2, OY + 110, "DITHR//CAM", 24, "#cfe3ff", "middle", "700"))
    o.append(text(OX + OW / 2, OY + 140, "SH1107 · 128x128 · I2C", 13, DIM, "middle"))
    for n, (x, y) in OPIN.items():
        o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="{GOLD}" stroke="#6b5321" stroke-width="2"/>')
        o.append(text(x, y - 18, n, 15, TXT, "middle"))

    # ---- кнопки
    def button(cx, cy, label, dashed=False):
        st = ' stroke-dasharray="6 5"' if dashed else ""
        s = [f'<rect x="{cx - 46}" y="{cy - 40}" width="92" height="80" rx="10" fill="#1d1f2a" stroke="#454a63" stroke-width="2"{st}/>',
             f'<circle cx="{cx}" cy="{cy}" r="22" fill="{"#e2496e" if not dashed else "#5b4a80"}"/>',
             text(cx, cy - 52, label, 14, DIM, "middle")]
        for dx in (-26, 26):
            s.append(f'<circle cx="{cx + dx}" cy="{cy + 40}" r="8" fill="{GOLD}" stroke="#6b5321" stroke-width="2"/>')
        return "".join(s)

    o.append(button(*B1, "спуск"))
    o.append(button(*B2, "кнопка 2", dashed=True))

    # ---- провода. Сначала нижние слои, поверх те, что ближе к зрителю.
    gx, gy = pin("L", "GND")
    o.append(wire(rpath([(gx, gy), (90, gy), (90, 760), (OPIN["GND"][0], 760), OPIN["GND"]]), GREY))
    o.append(wire(rpath([(B1[0] - 26, B1[1] + 40), (B1[0] - 26, 760)]), GREY))
    o.append(f'<circle cx="{B1[0] - 26}" cy="760" r="8" fill="{GREY}"/>')
    o.append(wire(rpath([(B2[0] + 26, B2[1] + 40), (B2[0] + 26, 760), (OPIN["GND"][0], 760)]), GREY, dash=True))

    x, y = pin("L", "IO12")
    o.append(wire(rpath([(x, y), (68, y), (68, 790), (B1[0] + 26, 790), (B1[0] + 26, B1[1] + 40)]), GRN))
    x, y = pin("L", "IO13")
    o.append(wire(rpath([(x, y), (46, y), (46, 820), (OPIN["SDA"][0], 820), OPIN["SDA"]]), BLUE))

    x, y = pin("R", "3V3")
    o.append(wire(rpath([(x, y), (700, y), (700, 395), (OPIN["VCC"][0], 395), OPIN["VCC"]]), RED))
    x, y = pin("R", "U0T")
    o.append(wire(rpath([(x, y), (640, y), (640, 420), (OPIN["SCL"][0], 420), OPIN["SCL"]]), YEL))
    x, y = pin("R", "U0R")
    o.append(wire(rpath([(x, y), (500, y), (500, 600), (B2[0] - 26, 600), (B2[0] - 26, B2[1] + 40)]), VIO, dash=True))

    # ---- легенда
    lx, ly = 880, 470
    items = [(RED, "VCC → 3V3"), (GREY, "GND → GND (у 5V)"), (BLUE, "SDA → IO13"), (YEL, "SCL → U0T (GPIO1)"),
             (GRN, "кнопка → IO12 и GND"), (VIO, "кнопка 2 → U0R (без MB)")]
    for i, (c, s) in enumerate(items):
        yy = ly + i * 28
        o.append(f'<rect x="{lx}" y="{yy - 9}" width="26" height="8" rx="4" fill="{c}"/>')
        o.append(text(lx + 36, yy, s, 14, TXT))
    o.append(text(600, 846, "microSD в слоте на ESP32-CAM · питание от повербанка в micro-USB платы MB", 14, DIM, "middle"))
    o.append("</svg>")
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text("\n".join(o), encoding="utf-8")
    print("written", OUT)


if __name__ == "__main__":
    main()
