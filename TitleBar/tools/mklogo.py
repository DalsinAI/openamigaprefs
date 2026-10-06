#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. OpenPrefs, MIT licence.
# Writes TitleBar/src/logo.h: AmigaChrome's logo as 16 x 16 RGB with a mask,
# from the web page's logo (web/amigachrome-logo-192.png in amigachrome).
#   tools/mklogo.py LOGO.png > src/logo.h
import sys
from PIL import Image
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
