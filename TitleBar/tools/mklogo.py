#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenPrefs, MIT licence.
# Writes TitleBar/src/logo.h: AmigaChrome's logo as 16 x 16 RGB with a mask,
# from the web page's logo (web/amigachrome-logo-192.png in amigachrome).
#   tools/mklogo.py LOGO.png > src/logo.h
#   tools/mklogo.py --big LOGO.png > src/logo_big.h   (0.6.11: 44 rows, ARGB with its alpha, cropped to the logo)
import sys
from PIL import Image
if sys.argv[1] == "--big":
    im = Image.open(sys.argv[2]).convert("RGBA")
    im = im.crop(im.getbbox())
    H = 44
    W = round(H * im.width / im.height)
    im = im.resize((W, H), Image.LANCZOS)
    print("/* AmigaChrome's logo, %d x %d, 0xAARRGGBB (alpha in the top byte), for a bar %d pixels high or less: drawn" % (W, H, H + 2))
    print(" * at the bar's height less 2, each pixel the average of the ones it covers (tools/mklogo.py --big). */")
    print("#define LOGO_BIG_W %d" % W)
    print("#define LOGO_BIG_H %d" % H)
    print("static const ULONG logo_big[LOGO_BIG_W * LOGO_BIG_H] = {")
    for y in range(H):
        print("    " + ", ".join("0x%02x%02x%02x%02x" % (a, r, g, b) for r, g, b, a in (im.getpixel((x, y)) for x in range(W))) + ",")
    print("};")
    sys.exit(0)
im = Image.open(sys.argv[1]).convert("RGBA").resize((16, 16), Image.LANCZOS)
print("/* AmigaChrome's logo, 16 x 16: 0xRRGGBB, or -1 where it is see-through (tools/mklogo.py) */")
print("static const long logo16[16 * 16] = {")
for y in range(16):
    row = []
    for x in range(16):
        r, g, b, a = im.getpixel((x, y))
        row.append("-1" if a < 128 else "0x%02x%02x%02x" % (r, g, b))
    print("    " + ", ".join(row) + ",")
print("};")
