# Library materials too heavy for the iPad

Found by the material controls bulk test (`paint/tests/material_controls_all.c`, run with `run_controls_bulk.py`) on
the iPad Pro 12.9" (5th generation), 2026-11-14/15. Each material was imported, compiled and rendered with its buffers
at the project resolution. The ones below hung or crashed (stopped after 10 s), and failed again when retried alone on
a freshly started app. "At import" means before its first preview.

## Too heavy (88)

| id | name | section | what happened |
|---|---|---|---|
| 215 | MDF Board | Wood | GPU error |
| 223 | Grassy Ground | Ground & Nature | GPU error |
| 240 | Overgrown Paving Stones | Tiles & Floors | GPU refused it (after its own errors) |
| 252 | Speckled MDF Board | Wood | GPU error |
| 425 | Rooted Stone Floor | Tiles & Floors | GPU error |
| 506 | Mossy Rock | Stone & Rock | GPU hang |
| 586 | Oak Bark | Wood | GPU error |
| 605 | Stone Brick Trim | Bricks & Walls | GPU error |
| 608 | Mossy Tree Bark | Wood | GPU hang |
| 610 | Nordic Stone Floor | Tiles & Floors | GPU hang, at import |
| 612 | Forest Floor | Ground & Nature | GPU error, at import |
| 703 | Smeared Clay | Ground & Nature | GPU error |
| 712 | Gilded Dragon Scales | Creatures & Organic | GPU error |
| 713 | Kintsugi | Patterns | GPU hang, at import |
| 720 | Layered Cliff Rock | Stone & Rock | GPU hang |
| 738 | Floating Magma Rocks | Stone & Rock | GPU error |
| 745 | Organic Cell Tiles | Creatures & Organic | GPU hang |
| 751 | Celestial Marble Tiles | Tiles & Floors | GPU error |
| 851 | Autumn Garden | Ground & Nature | GPU error |
| 863 | Mountain Path | Ground & Nature | GPU error |
| 872 | Pirate Coins | Metal | GPU hang |
| 879 | Scorched Ground | Ground & Nature | GPU hang, at import |
| 908 | Weathered Sandstone Slab | Stone & Rock | GPU refused it (after its own errors) |
| 925 | Mud Brick Wall | Bricks & Walls | GPU hang, at import |
| 948 | Bricks with Mortar | Bricks & Walls | GPU error |
| 969 | Construction Debris | Ground & Nature | GPU error |
| 971 | Rustic Brick Wall | Bricks & Walls | GPU error |
| 975 | Broken floor | Tiles & Floors | GPU error, at import |
| 976 | Mosaic Pavement | Tiles & Floors | GPU error |
| 985 | Grass Clumps | Ground & Nature | GPU hang |
| 996 | Gold-Inlaid Marble Floor | Tiles & Floors | GPU hang, at import |
| 1022 | Mossy Stone | Stone & Rock | GPU hang |
| 1076 | Tufted Leather Couch | Fabric & Leather | GPU error, at import |
| 1077 | Ceramic Roof Tiles | Tiles & Floors | GPU error, at import |
| 1081 | Mosaic Tiles | Tiles & Floors | GPU error |
| 1096 | Scree Rocks | Stone & Rock | GPU hang |
| 1097 | Dry Stone Wall | Bricks & Walls | crashed the app |
| 1100 | Ivy-Covered Dry Stone Wall | Bricks & Walls | crashed the app |
| 1104 | Infected rock | Stone & Rock | GPU hang |
| 1105 | Fossil Sandstone | Stone & Rock | GPU hang, at import |
| 1111 | Arabesque Stone Tiles | Tiles & Floors | GPU error |
| 1117 | Mossy Metal Tiles | Metal | GPU hang |
| 1126 | Concrete Rubble | Stone & Rock | GPU hang |
| 1140 | Chunky Brick Wall | Bricks & Walls | GPU hang |
| 1181 | Stylized Strata Rock | Stone & Rock | GPU error, at import |
| 1187 | Crystal Moss | Ground & Nature | GPU refused it (after its own errors) |
| 1195 | Riveted Rusty Panels | Metal | GPU hang |
| 1215 | Stylized Rock Cliff | Stone & Rock | GPU hang, at import |
| 1217 | UV Grid | Patterns | GPU error |
| 1221 | Glowing Flowing Lava | Stone & Rock | GPU hang, at import |
| 1230 | Rustic Wood Flooring | Wood | GPU refused it (after its own errors) |
| 1231 | Diamond Patterned Tiles | Tiles & Floors | GPU hang |
| 1243 | Rusty Peeling Paint | Metal | GPU refused it (after its own errors) |
| 1244 | Mud | Ground & Nature | GPU hang |
| 1251 | Leaky Rust Grunge | Metal | GPU error |
| 1265 | Cobweb Wall | Bricks & Walls | GPU hang |
| 1291 | Rock Formation | Stone & Rock | GPU error |
| 1298 | Damaged Tiles | Tiles & Floors | GPU error |
| 1306 | Damaged Concrete Tiles | Tiles & Floors | GPU error, at import |
| 1310 | Dune Sandstone Bricks | Bricks & Walls | GPU refused it (after its own errors) |
| 1323 | Obsidian Rock | Stone & Rock | GPU refused it (after its own errors) |
| 1337 | Mossy Stone Floor | Tiles & Floors | GPU hang |
| 1341 | Small Pebbles | Stone & Rock | GPU refused it (after its own errors) |
| 1348 | Rough Marble | Stone & Rock | GPU refused it (after its own errors) |
| 1349 | Polished Marble | Stone & Rock | GPU refused it (after its own errors) |
| 1352 | Weathered Bunker Planks | Wood | GPU refused it (after its own errors) |
| 1356 | Bunker Rock | Stone & Rock | GPU refused it (after its own errors) |
| 1370 | Cobwebbed Rock | Stone & Rock | GPU refused it (after its own errors) |
| 1417 | Hollow Terracotta Bricks | Bricks & Walls | GPU refused it (after its own errors) |
| 1538 | Scree | Stone & Rock | GPU error |
| 1580 | Sandy Bricks | Bricks & Walls | GPU refused it (after its own errors) |
| 1585 | Classic Flower Net Tiles | Tiles & Floors | GPU error |
| 1590 | Strata Rock (Pixel Art) | Stone & Rock | GPU error, at import |
| 1619 | Coal Pile | Stone & Rock | GPU hang, at import |
| 1621 | OSB | Wood | GPU refused it (after its own errors) |
| 1637 | Overgrown Aggregate Tiles | Tiles & Floors | GPU hang |
| 1651 | Coral Seabed | Creatures & Organic | GPU hang, at import |
| 1664 | Jade | Stone & Rock | GPU error, at import |
| 1670 | Cracked Lava Crust | Stone & Rock | GPU refused it (after its own errors) |
| 1693 | metal_grid_rust | Metal | GPU hang, at import |
| 1698 | Blue patterned worn tiles | Tiles & Floors | GPU refused it (after its own errors) |
| 1730 | Bloody Pebbles | Stone & Rock | GPU error |
| 1798 | Faceted Rock | Stone & Rock | GPU hang, at import |
| 1806 | Lime Washed Plaster | Bricks & Walls | GPU error |
| 1815 | Dungeon Bricks | Bricks & Walls | GPU refused it (after its own errors) |
| 1844 | Rock (Pixel Art) | Stone & Rock | GPU hang |
| 1932 | Herringbone Wood | Wood | GPU hang |
| 1964 | Stone Slab Wall | Bricks & Walls | GPU hang |

## Slow (85)

They render, but their buffers take over 10 s at full size on the iPad ("never": still not done when the test gave up).
Painting with them works from the lower tiers meanwhile.

| id | name | section | buffers |
|---|---|---|---|
| 876 | Toxic Slime | Ground & Nature | never |
| 1665 | Striped Sandstone | Stone & Rock | never |
| 483 | Medieval Stone Floor | Tiles & Floors | never |
| 614 | Water Level Mask | Ground & Nature | never |
| 654 | Rocks | Stone & Rock | never |
| 1106 | Sulfur Crystals | Stone & Rock | never |
| 615 | Cracked Dirt | Ground & Nature | never |
| 878 | Cracked Overgrown Tiles | Tiles & Floors | never |
| 1098 | Fortress Wall | Bricks & Walls | never |
| 1138 | Painted Tile Floor | Tiles & Floors | never |
| 1158 | Arc Pavement | Tiles & Floors | never |
| 548 | Stone Pavement | Tiles & Floors | never |
| 867 | Frozen Cliff | Stone & Rock | never |
| 1108 | Rusty Metal Plates | Metal | never |
| 1293 | French Stone Pavement | Tiles & Floors | never |
| 1622 | Barren Rocky Sand | Ground & Nature | never |
| 617 | Boulder Stone Wall | Bricks & Walls | never |
| 643 | Stone Path | Tiles & Floors | never |
| 881 | Firewood Stack | Wood | never |
| 1530 | Terracotta Roof Tiles | Tiles & Floors | never |
| 1539 | Retro Ceramic Tiles | Tiles & Floors | never |
| 1714 | Mossy Roof Shingles | Tiles & Floors | never |
| 1933 | Terracotta Bricks | Bricks & Walls | never |
| 744 | Cobblestone | Tiles & Floors | never |
| 1101 | Floral Ceramic Tiles | Tiles & Floors | never |
| 1673 | Cracked Desert Soil | Ground & Nature | never |
| 508 | Layered Riverbank Rock | Stone & Rock | never |
| 541 | Dirty Cracked Tiles | Tiles & Floors | never |
| 873 | Hex Tiles | Tiles & Floors | never |
| 1644 | Sandstone Temple Wall | Bricks & Walls | never |
| 1734 | Sakura Garden Ground | Ground & Nature | never |
| 894 | Alhambra Checkered Tiles | Tiles & Floors | never |
| 982 | Seigaiha Cobblestone | Tiles & Floors | never |
| 1115 | Trench Wall | Wood | never |
| 1676 | Hexagonal Wood Parquet | Wood | never |
| 1623 | Bronze Spiked Wall | Metal | 38.3 s |
| 158 | Muddy Dirt Ground | Ground & Nature | 35.6 s |
| 1122 | Petals on Wood | Ground & Nature | 32.0 s |
| 54 | Rippled Concrete | Stone & Rock | 29.1 s |
| 493 | Rough Stone Floor | Tiles & Floors | 26.1 s |
| 1113 | Rusty Corrugated Panels | Metal | 24.5 s |
| 697 | Spackled Wall | Bricks & Walls | 22.4 s |
| 1188 | Honeycomb | Food | 22.3 s |
| 1894 | Sandy Dirt Ground | Ground & Nature | 21.6 s |
| 1103 | Magma Rocks | Stone & Rock | 21.6 s |
| 983 | Pyrite Vein | Stone & Rock | 21.3 s |
| 479 | Carpet | Fabric & Leather | 21.3 s |
| 1744 | Yin Yang Tiles | Tiles & Floors | 21.2 s |
| 1078 | Bricks with Sloppy Mortar | Bricks & Walls | 20.9 s |
| 1898 | Floral Pattern | Patterns | 20.3 s |
| 911 | Clover Forest Ground | Ground & Nature | 19.8 s |
| 1088 | Mossy Roof Tiles | Tiles & Floors | 19.4 s |
| 591 | Elven Shingles | Tiles & Floors | 22.8 s |
| 609 | Stone Floor | Tiles & Floors | 19.0 s |
| 923 | Mossy Dirty Pavement | Tiles & Floors | 18.8 s |
| 1112 | Damaged Concrete | Stone & Rock | 18.1 s |
| 1093 | OSB Board | Wood | 18.1 s |
| 1985 | Rounded Cobblestone | Tiles & Floors | 17 s |
| 1255 | Tree Bark | Wood | 16.1 s |
| 837 | Flowery Stone Garden | Ground & Nature | 16.0 s |
| 616 | Sci-Fi Wall Panels | Sci-Fi & Tech | 15.4 s |
| 748 | Vintage Luggage | Fabric & Leather | 14.9 s |
| 452 | 3D Printed Layered Concrete | Stone & Rock | 14.7 s |
| 1655 | Ground Tiles | Tiles & Floors | 14.6 s |
| 59 | Pine Bark | Wood | 14.3 s |
| 1733 | Damaged Asphalt | Stone & Rock | 14.3 s |
| 438 | Damaged Plaster Brick Wall | Bricks & Walls | 14.1 s |
| 1645 | Cratered Moon | Stone & Rock | 14.1 s |
| 558 | Rough Sandstone | Stone & Rock | 13.4 s |
| 228 | Mossy Rocky Ground | Ground & Nature | 12.8 s |
| 619 | Lawn Grass | Ground & Nature | 12.7 s |
| 118 | Rusted Sci-Fi Metal Panels | Sci-Fi & Tech | 12.6 s |
| 611 | Old Tiles with Roots | Tiles & Floors | 12.6 s |
| 1475 | Rough Stone Bricks | Bricks & Walls | 12.5 s |
| 997 | Ascent dog | Bricks & Walls | 12.4 s |
| 453 | Stitched Flesh | Creatures & Organic | 12.1 s |
| 1522 | Pebbly Sand | Ground & Nature | 11.9 s |
| 55 | Mossy Stone Brick Wall | Bricks & Walls | 11.7 s |
| 360 | Grungy Bricks | Bricks & Walls | 11.4 s |
| 343 | Concrete Floor | Tiles & Floors | 11.3 s |
| 575 | Lumpy Moss | Ground & Nature | 10.9 s |
| 1089 | Granite Countertop | Stone & Rock | 10.7 s |
| 529 | Rusty Gears | Metal | 10.5 s |
| 1403 | Medieval Stone Brick Wall | Bricks & Walls | 10.3 s |
| 722 | Sandstone Cliff | Stone & Rock | 10.1 s |

## Other problems (not from the controls)

- Shaders that partly fail to compile (the rest of the material renders):
  - 137 Screws and Nuts: `PI` defined twice
  - 170 Wool Fabric: a missing `;` in its own code
  - 230 Hexagon Tiles, 529 Rusty Gears: a function defined twice
  - 932 Carved Stone, 939 Peeling Birch Bark: `sample` used as a name
- 485 Worn Brick Courses renders differently on each import (21% of the base color), without controls too.
- 983 Pyrite Vein: a Blend node samples too many buffers (25 textures), so it may render wrong.
- 643 Stone Path, 982 Seigaiha Cobblestone, 1106 Sulfur Crystals and 1676 Hexagonal Wood Parquet look different at their
  defaults in the test, but their buffers never finish on the iPad, so that comparison is not reliable (643 also hung
  the GPU once).
