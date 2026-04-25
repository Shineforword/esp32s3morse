#!/usr/bin/env python3
"""将 main/tank.png 缩放铺满 LCD_H_RES×LCD_V_RES 后生成 main/tank_rgb565.c。"""
from __future__ import annotations

import argparse
from pathlib import Path

try:
    from PIL import Image, ImageOps
except ImportError as e:
    raise SystemExit("需要 Pillow：pip install pillow") from e


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--src", type=Path, default=Path("main/tank.png"))
    p.add_argument("--out", type=Path, default=Path("main/tank_rgb565.c"))
    p.add_argument("--w", type=int, default=240)
    p.add_argument("--h", type=int, default=280)
    args = p.parse_args()
    W, H = args.w, args.h

    src = Image.open(args.src).convert("RGBA")
    fitted = ImageOps.fit(src, (W, H), Image.Resampling.LANCZOS)
    white = Image.new("RGBA", (W, H), (255, 255, 255, 255))
    img = Image.alpha_composite(white, fitted).convert("RGB")
    flat = img.tobytes()
    n = W * H
    lines = []
    for i in range(n):
        o = i * 3
        r, g, b = flat[o], flat[o + 1], flat[o + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        lines.append(f"0x{v:04x}")
    body = ",\n    ".join(
        ", ".join(lines[j : j + 12]) for j in range(0, len(lines), 12)
    )
    c = (
        f"/* 由 tools/gen_tank_rgb565.py 从 {args.src.as_posix()} 生成，勿手改大数组 */\n"
        "#include <stdint.h>\n\n"
        f"const int tank_img_w = {W};\n"
        f"const int tank_img_h = {H};\n"
        f"const uint16_t tank_img_rgb565[{W * H}] = {{\n    {body}\n}};\n"
    )
    args.out.write_text(c, encoding="utf-8")
    print("written", args.out, W, H, "pixels", n)


if __name__ == "__main__":
    main()
