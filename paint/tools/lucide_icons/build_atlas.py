#!/usr/bin/env python3
"""Puts the rendered Lucide tiles into assets/icons05x.png, icons.png and icons2x.png.

Usage: python3 build_atlas.py <tiles dir>   (tiles from render.mjs)
Slots not listed in icons.json keep their original tile. Each icon keeps the brightness
of the tile it replaces, so gray tool icons stay gray.
"""
import json, os, sys
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
assets = os.path.join(here, "..", "..", "assets")
tiles = sys.argv[1]
icons = [k for k in json.load(open(os.path.join(here, "icons.json"))) if not k.startswith("_")]

for name, tile in (("icons05x.png", 25), ("icons.png", 50), ("icons2x.png", 100)):
    path = os.path.join(assets, name)
    atlas = Image.open(path).convert("LA")
    out = atlas.copy()
    eye = tile * 18 // 50
    for slot in icons:
        icon = Image.open(os.path.join(tiles, f"{slot}_{eye if slot.startswith('eye') else tile}.png")).convert("RGBA")
        if slot.startswith("eye"):
            i = 0 if slot == "eye_on" else 1
            box = (i * eye, atlas.height - eye, (i + 1) * eye, atlas.height)
        else:
            i = int(slot)
            box = ((i % 12) * tile, (i // 12) * tile, (i % 12 + 1) * tile, (i // 12 + 1) * tile)
        # Brightness of the original glyph
        old = atlas.crop(box)
        px = [l for l, a in old.getdata() if a > 128]
        level = max(180, round(sum(px) / len(px))) if px else 255  # No dark icons
        alpha = icon.getchannel("A")
        glyph = Image.merge("LA", (Image.new("L", icon.size, level), alpha))
        cell = Image.new("LA", (box[2] - box[0], box[3] - box[1]), (0, 0))
        cell.paste(glyph, ((cell.width - icon.width) // 2, (cell.height - icon.height) // 2))
        out.paste(cell, box[:2])
    out.save(path, optimize=True)
    print("wrote", name)
