#!/usr/bin/env python3
"""Test different PIL options for terminal cover art rendering.

Usage: python test_cover_art.py <image_file> [width] [height]
  width/height default to 60 and 20 (terminal cells)
"""

import sys
from PIL import Image, ImageEnhance, ImageFilter

HALF_BLOCK = "\u2580"
WIDTH = 60
HEIGHT = 20


def rgb_to_256(r, g, b):
    if abs(r - g) < 10 and abs(g - b) < 10:
        gray = (r + g + b) // 3
        if gray < 8:
            return 16
        if gray > 248:
            return 231
        return round((gray - 8) / 247 * 23) + 232
    ri = round(r / 255 * 5)
    gi = round(g / 255 * 5)
    bi = round(b / 255 * 5)
    return 16 + 36 * ri + 6 * gi + bi


def render(src_img, w, h, resampler=Image.LANCZOS):
    """Render image using half-block chars with 256 ANSI colors."""
    pixel_h = h * 2
    resized = src_img.resize((w, pixel_h), resampler)
    pixels = list(resized.getdata())
    lines = []
    for row in range(h):
        top_y = row * 2
        bot_y = top_y + 1
        line = ""
        for col in range(w):
            tr, tg, tb = pixels[top_y * w + col]
            br, bg, bb = pixels[bot_y * w + col]
            fg = rgb_to_256(tr, tg, tb)
            bg_c = rgb_to_256(br, bg, bb)
            line += f"\033[38;5;{fg}m\033[48;5;{bg_c}m{HALF_BLOCK}"
        line += "\033[0m"
        lines.append(line)
    return "\n".join(lines)


def header(title):
    print(f"\n\033[1m{'=' * 60}")
    print(f"  {title}")
    print(f"{'=' * 60}\033[0m")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    path = sys.argv[1]
    width = int(sys.argv[2]) if len(sys.argv) > 2 else WIDTH
    height = int(sys.argv[3]) if len(sys.argv) > 3 else HEIGHT

    img = Image.open(path).convert("RGB")
    print(f"Source: {path}  ({img.size[0]}x{img.size[1]})  Render: {width}x{height} cells")

    # --- 1. Resampling methods ---
    resamplers = [
        ("NEAREST",  Image.NEAREST),
        ("BILINEAR", Image.BILINEAR),
        ("BICUBIC",  Image.BICUBIC),
        ("LANCZOS",  Image.LANCZOS),
        ("BOX",      Image.BOX),
        ("HAMMING",  Image.HAMMING),
    ]

    for name, method in resamplers:
        header(f"Resampling: {name}")
        print(render(img, width, height, method))

    # --- 2. Enhancements (all using LANCZOS) ---
    enhancements = [
        ("No enhancement (baseline)", None),
        ("Sharpness 1.5", lambda i: ImageEnhance.Sharpness(i).enhance(1.5)),
        ("Sharpness 2.0", lambda i: ImageEnhance.Sharpness(i).enhance(2.0)),
        ("Contrast 1.3", lambda i: ImageEnhance.Contrast(i).enhance(1.3)),
        ("Contrast 1.5", lambda i: ImageEnhance.Contrast(i).enhance(1.5)),
        ("Brightness 1.1 + Contrast 1.2",
         lambda i: ImageEnhance.Contrast(ImageEnhance.Brightness(i).enhance(1.1)).enhance(1.2)),
        ("Color saturation 1.3", lambda i: ImageEnhance.Color(i).enhance(1.3)),
        ("Color 1.5 + Contrast 1.3",
         lambda i: ImageEnhance.Contrast(ImageEnhance.Color(i).enhance(1.5)).enhance(1.3)),
        ("Sharpen filter + Contrast 1.2",
         lambda i: ImageEnhance.Contrast(i.filter(ImageFilter.SHARPEN)).enhance(1.2)),
        ("Detail filter + Color 1.3",
         lambda i: ImageEnhance.Color(i.filter(ImageFilter.DETAIL)).enhance(1.3)),
    ]

    for name, enhance_fn in enhancements:
        header(f"Enhancement: {name}")
        test_img = img.copy()
        if enhance_fn:
            test_img = enhance_fn(test_img)
        print(render(test_img, width, height))


if __name__ == "__main__":
    main()
