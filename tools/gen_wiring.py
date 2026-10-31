"""Схема подключения DITHR//CAM в SVG: docs/wiring.svg.

Одна сенсорная кнопка TTP223, экран SH1107 по I2C. Порядок штырей ESP32-CAM
такой же, как в редакторе схем olerast.com/tools/schematic: антенна сверху,
5V и 3V3 внизу.

    python tools/gen_wiring.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "docs" / "wiring.svg"
FONT = "ui-monospace, SFMono-Regular, Consolas, 'Liberation Mono', monospace"

BG, PCB, PCB_EDGE = "#0e0f17", "#16261f", "#2c4a3b"
GOLD, TXT, DIM = "#d9b25a", "#e8eaf2", "#8a8fa6"
RED, GREY, BLUE, YEL, GRN = "#ff5a5f", "#c3c7d6", "#4ea8ff", "#ffd23f", "#3ee08a"

W, H = 1200, 900
# ESP32-CAM: сверху вниз, как на плате антенной вверх
LEFT = ["IO4", "IO2", "IO14", "IO15", "IO13", "IO12", "GND", "5V"]
RIGHT = ["GND", "U0T", "U0R", "VCC", "GND2", "IO0", "IO16", "3V3"]
BX, BY, BW, BH = 150, 40, 300, 660
LX, RX = BX + 32, BX + BW - 32
PY0, PSTEP = BY + 360, 40


def pin(side, name):
    col = LEFT if side == "L" else RIGHT
    return (LX if side == "L" else RX, PY0 + col.index(name) * PSTEP)


# Экран SH1107, выводы снизу слева направо как на модуле: SDA SCL GND VCC
OX, OY, OW, OH = 700, 40, 380, 280
OPIN = {n: (OX + 70 + i * 80, OY + OH) for i, n in enumerate(["SDA", "SCL", "GND", "VCC"])}
# Сенсорный модуль TTP223, выводы снизу
TX, TY, TW, TH = 800, 450, 230, 170
TPIN = {n: (TX + 50 + i * 65, TY + TH) for i, n in enumerate(["VCC", "IO", "GND"])}


def wire(d, color, w=5):
    return (f'<path d="{d}" fill="none" stroke="#05060a" stroke-width="{w + 5}" stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/>')


def rpath(pts, r=20):
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


def dot(x, y, c):
    return f'<circle cx="{x}" cy="{y}" r="8" fill="{c}" stroke="#05060a" stroke-width="3"/>'


def main():
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">',
         f'<rect width="{W}" height="{H}" rx="22" fill="{BG}"/>']

    # ---- ESP32-CAM
    o.append(f'<rect x="{BX}" y="{BY}" width="{BW}" height="{BH}" rx="16" fill="{PCB}" stroke="{PCB_EDGE}" stroke-width="3"/>')
    zz = " ".join(f"L{BX + 40 + i * 22},{BY + (26 if i % 2 else 70)}" for i in range(1, 11))
    o.append(f'<path d="M{BX + 40},{BY + 70} {zz}" fill="none" stroke="#6f7a8e" stroke-width="4"/>')
    o.append(text(BX + BW / 2, BY + 100, "ESP32-CAM", 22, TXT, "middle", "700"))
    o.append(f'<rect x="{BX + BW / 2 - 50}" y="{BY + 120}" width="100" height="100" rx="12" fill="#0b0c12" stroke="#3a3f55" stroke-width="2"/>')
    o.append(f'<circle cx="{BX + BW / 2}" cy="{BY + 170}" r="32" fill="#14161f" stroke="#4a5070" stroke-width="3"/>')
    o.append(f'<circle cx="{BX + BW / 2}" cy="{BY + 170}" r="14" fill="#2b3a6a"/>')
    o.append(text(BX + BW / 2, BY + 242, "OV2640 / OV3660", 13, DIM, "middle"))
    o.append(f'<rect x="{BX + BW / 2 - 60}" y="{BY + 262}" width="120" height="70" rx="6" fill="#8a8f9e" opacity="0.45"/>')
    o.append(text(BX + BW / 2, BY + 302, "microSD", 15, TXT, "middle"))
    for side, col in (("L", LEFT), ("R", RIGHT)):
        for n in col:
            x, y = pin(side, n)
            label = "GND" if n == "GND2" else n
            o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="{GOLD}" stroke="#6b5321" stroke-width="2"/>')
            o.append(text(x + (18 if side == "L" else -18), y + 5, label, 15, TXT, "start" if side == "L" else "end"))

    # ---- экран
    o.append(f'<rect x="{OX}" y="{OY}" width="{OW}" height="{OH}" rx="14" fill="#141724" stroke="#3a3f5a" stroke-width="3"/>')
    o.append(f'<rect x="{OX + 50}" y="{OY + 28}" width="{OW - 100}" height="{OH - 108}" rx="6" fill="#020203"/>')
    o.append(text(OX + OW / 2, OY + 105, "DITHR//CAM", 24, "#cfe3ff", "middle", "700"))
    o.append(text(OX + OW / 2, OY + 136, "OLED 1.5\" SH1107 · 128x128 · I2C", 13, DIM, "middle"))
    for n, (x, y) in OPIN.items():
        o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="{GOLD}" stroke="#6b5321" stroke-width="2"/>')
        o.append(text(x, y - 18, n, 15, TXT, "middle"))

    # ---- сенсорная кнопка TTP223
    o.append(f'<rect x="{TX}" y="{TY}" width="{TW}" height="{TH}" rx="12" fill="#13305f" stroke="#2c4f8a" stroke-width="3"/>')
    o.append(f'<circle cx="{TX + TW / 2}" cy="{TY + 62}" r="40" fill="#1d3f78" stroke="#8fb4ff" stroke-width="3"/>')
    o.append(f'<circle cx="{TX + TW / 2}" cy="{TY + 62}" r="26" fill="none" stroke="#8fb4ff" stroke-width="2" opacity="0.6"/>')
    o.append(text(TX + TW / 2, TY - 14, "сенсорная кнопка TTP223", 14, DIM, "middle"))
    for n, (x, y) in TPIN.items():
        o.append(f'<circle cx="{x}" cy="{y}" r="9" fill="{GOLD}" stroke="#6b5321" stroke-width="2"/>')
        o.append(text(x, y - 18, "I/O" if n == "IO" else n, 15, TXT, "middle"))

    # ---- провода: сначала земля и питание, поверх сигнальные
    g = pin("L", "GND")
    o.append(wire(rpath([g, (110, g[1]), (110, 760), (TPIN["GND"][0], 760), TPIN["GND"]]), GREY))
    o.append(wire(rpath([(TPIN["GND"][0], 760), (1150, 760), (1150, 350), (OPIN["GND"][0], 350), OPIN["GND"]]), GREY))
    o.append(dot(TPIN["GND"][0], 760, GREY))

    v = pin("R", "3V3")
    o.append(wire(rpath([v, (1110, v[1]), (1110, 380), (OPIN["VCC"][0], 380), OPIN["VCC"]]), RED))
    o.append(wire(rpath([TPIN["VCC"], (TPIN["VCC"][0], v[1])]), RED))
    o.append(dot(TPIN["VCC"][0], v[1], RED))

    x, y = pin("L", "IO12")
    o.append(wire(rpath([TPIN["IO"], (TPIN["IO"][0], 735), (80, 735), (80, y), (x, y)]), GRN))
    x, y = pin("L", "IO13")
    o.append(wire(rpath([(x, y), (50, y), (50, 790), (640, 790), (640, 410), (OPIN["SDA"][0], 410), OPIN["SDA"]]), BLUE))
    x, y = pin("R", "U0T")
    o.append(wire(rpath([(x, y), (600, y), (600, 330), (OPIN["SCL"][0], 330), OPIN["SCL"]]), YEL))

    # ---- легенда
    items = [(60, RED, "3V3 → VCC экрана и TTP223"), (340, GREY, "GND → GND экрана и TTP223"),
             (620, BLUE, "IO13 → SDA"), (780, YEL, "U0T (GPIO1) → SCL"), (980, GRN, "IO12 → I/O TTP223")]
    for lx, c, s in items:
        o.append(f'<rect x="{lx}" y="{836 - 9}" width="22" height="8" rx="4" fill="{c}"/>')
        o.append(text(lx + 30, 836, s, 13, TXT))
    o.append(text(W / 2, H - 22, "microSD в слоте на ESP32-CAM · питание: повербанк в micro-USB платы ESP32-CAM-MB", 13, DIM, "middle"))
    o.append("</svg>")
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text("\n".join(o), encoding="utf-8")
    print("written", OUT)


if __name__ == "__main__":
    main()
