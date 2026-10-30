"""Синий шум 64x64 методом void-and-cluster (Ulichney, 1993).

Каждому пикселю присваивается ранг 0..4095 так, что точки любого уровня
распределены равномерно, без узора и без комков. Порог из такой таблицы
даёт «стохастический» дизеринг, как у хорошей печати.

Запуск: python tools/gen_bluenoise.py  ->  firmware/components/fx/fx_bluenoise.c
"""
from pathlib import Path

import numpy as np

N = 64
SIGMA = 1.5


def kernel():
    d = np.minimum(np.arange(N), N - np.arange(N))
    g = np.exp(-(d[:, None] ** 2 + d[None, :] ** 2) / (2 * SIGMA ** 2))
    return g


K = kernel()


def energy_add(E, y, x, sign):
    E += sign * np.roll(np.roll(K, y, 0), x, 1)


def main():
    rng = np.random.default_rng(1)
    pat = np.zeros((N, N), bool)
    pat.flat[rng.choice(N * N, N * N // 10, replace=False)] = True
    E = np.real(np.fft.ifft2(np.fft.fft2(pat.astype(float)) * np.fft.fft2(K)))
    # 1. Расталкиваем начальные точки: самая тесная в самую пустую дыру
    while True:
        c = np.argmax(np.where(pat, E, -1e9))
        pat.flat[c] = False
        energy_add(E, *divmod(c, N), -1)
        v = np.argmin(np.where(pat, 1e9, E))
        if v == c:
            pat.flat[c] = True
            energy_add(E, *divmod(c, N), 1)
            break
        pat.flat[v] = True
        energy_add(E, *divmod(v, N), 1)
    rank = np.zeros((N, N), int)
    ones = int(pat.sum())
    # 2. Ранги начальных точек: убираем самые тесные
    p2, E2 = pat.copy(), E.copy()
    for r in range(ones - 1, -1, -1):
        c = np.argmax(np.where(p2, E2, -1e9))
        p2.flat[c] = False
        energy_add(E2, *divmod(c, N), -1)
        rank.flat[c] = r
    # 3. Дальше заполняем самые пустые места
    for r in range(ones, N * N):
        v = np.argmin(np.where(pat, 1e9, E))
        pat.flat[v] = True
        energy_add(E, *divmod(v, N), 1)
        rank.flat[v] = r
    t = (rank * 256 // (N * N)).astype(np.uint8)
    out = ["// Сгенерировано tools/gen_bluenoise.py, руками не править.\n",
           '#include "fx_internal.h"\n\n', "const uint8_t fx_bluenoise[64][64] = {\n"]
    for row in t:
        out.append("    {" + ", ".join(str(int(v)) for v in row) + "},\n")
    out.append("};\n")
    dst = Path(__file__).resolve().parent.parent / "firmware" / "components" / "fx" / "fx_bluenoise.c"
    dst.write_text("".join(out), encoding="utf-8")
    print("written", dst, "mean", t.mean())


if __name__ == "__main__":
    main()
