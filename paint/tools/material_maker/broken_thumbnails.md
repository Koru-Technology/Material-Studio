# Library materials that still render broken

These 9 materials compile but render one flat color, black or magenta, so their thumbnail in thumbs.pack is black or
magenta. The materials themselves are broken: adding one to a project shows the same thing. Found by re-rendering their
thumbnails with `paint/tools/material_maker/mm_thumbnails.c` on the Mac (2026-11-14), which reported no shader failure for
them; the cause is not known yet.

Once one is fixed, delete its `<id>.jpg` from `<save folder>/mm_thumbs` and run the thumbnail script again: it renders only
the thumbnails missing from that cache and writes thumbs.pack from it (copy it to `paint/assets/material_maker/`).

12 more were removed from the library (overrides.json, report.md): 11 whose generated shader does not compile (functions
called under old names or signatures, a function defined twice, `output` used as a name) and one that hangs the GPU.

Their names, style and section in `curation.json` were set without a usable preview (from the original name and tags):
check them once they render. Name them with the rules below.

| id | original name | name now | style | section | cause |
|---|---|---|---|---|---|
| 650 | Celtic Know Wood Carving | Celtic Know Wood Carving | realistic | Wood | compiles, renders black |
| 975 | Broken floor | Broken floor | stylized | Tiles & Floors | compiles, renders black |
| 988 | Wet coastal sand | Wet coastal sand | stylized | Ground & Nature | compiles, renders black |
| 997 | Ascent dog | Ascent dog | stylized | Bricks & Walls | compiles, renders black |
| 1104 | Infected rock | Infected rock | stylized | Stone & Rock | compiles, renders black |
| 1181 | Stylized Strata Rock | Stylized Strata Rock | stylized | Stone & Rock | compiles, renders black |
| 1215 | Stylized Rock Cliff | Stylized Rock Cliff | stylized | Stone & Rock | compiles, renders black |
| 1693 | metal_grid_rust | metal_grid_rust | realistic | Metal | compiles, renders magenta |
| 1698 | Blue patterned worn tiles | Blue patterned worn tiles | realistic | Tiles & Floors | compiles, renders magenta |

Earlier, 53 more materials had a broken thumbnail: two runs of ids (1230-1287 and 1695-1719) had all been given the
image of the material rendered before them, and 1187 had an empty entry. They rendered correctly when rendered again and
are fixed in thumbs.pack.

## Naming rules

The prompt the names were made with (Claude Sonnet 5.5, a 128px preview plus the current name, section, tags and kind):

> You name the built-in materials of Material Studio, a 3D texture painting app. Its material library ships ~1000 community materials from materialmaker.org. Their original names are inconsistent: snake_case, author handles, challenge tags, version numbers, typos, game and film references. Give each one a clean, standard, human-readable display name, the kind a user expects in a professional material library.
>
> You get, per material: its id, its current name, its library section, its tags, its kind, and (usually) a 128x128 preview of the material on a sphere. Use all of it. The current name often carries real information the preview cannot show (a wood or stone species, a place or tradition such as Alhambra, Moroccan, Seigaiha, Kintsugi, Terrazzo): keep that information when the preview agrees with it. Use the preview to check the name, to choose good descriptive words, and to replace names that say nothing (gibberish, "Pattern_010", "Interesting Rock", "wall", in-jokes). Keep the subject the author gave (feathers stay feathers, a waffle stays a waffle) unless it is clearly wrong: the preview is small and the author knows what they made. Do not pile on adjectives the original name did not need; add a quality only when it helps tell the material apart.
>
> Style:
> - Title Case. Lower case for short joining words (a, an, and, of, on, with, in, the, for, or, to, by) unless first.
> - US spelling: Armor, Aluminum, Mold, Fiber, Color.
> - Usually 1 to 4 words, at most 5. Name the surface: what it is, plus the qualities that matter (condition, finish, pattern, shape, scale). Good examples: "Mossy Cobblestone", "Brick Wall", "Worn Oak Planks", "Hammered Copper", "Roof Tiles", "Cracked Desert Soil", "Ceramic Tiles", "Chain Mail".
> - No colors in names: no red, pink, blue, green, gray, white, black, yellow, orange, purple, brown, beige, golden, rainbow, colorful, multicolored, and no tones such as light, dark or pale ("Pink Brain Tissue" is "Brain Tissue", "White Marble" is "Marble", "Red Brick Wall" is "Brick Wall", "Dark Wood" is "Wood"). Only keep a color word that is part of the established name of the material itself, not a description of it: White Chocolate, Dark Chocolate, Milk Chocolate. Metals are materials, not colors: Gold, Silver, Copper, Bronze, Brass stay ("Gold Coins", "Hammered Silver"). Gilded, rusty, mossy and the like are qualities, not colors.
> - Prefer the plural that a surface naturally has: "Bricks", "Planks", "Tiles", "Pebbles"; "Brick Wall" and "Tile Floor" are fine.
> - No style words in names: never "Stylized", "Stylised", "Realistic", "Toon", "Hand-Painted", "Cartoon", "Painterly", "Anime". The library shows the style as a category: you report it separately (below), not in the name.
> - Animated materials (kind material_dynamic) end with " (Animated)". Pixel-art materials end with " (Pixel Art)". No other parentheses.
> - Remove: version numbers and numbering (01, _1, V2, v1, "2", "3"), author handles and prefixes (n_, fk_, sr_, NP_, "- Amirthaz"), challenge and event tags (Mayterials, Mayterials2022, Nodevember, MonthlyChallenge, "Day 3,4", "Noding live"), and words about how it was made or what file it is: Material, Texture, Shader, Node, Tutorial, Generator, Base, Basic, Simple, Quick, Easy, Test, Prototype, PBR, "in 22 Nodes", "1K version", "Remake", "Reworked", "with Comments", "Try".
> - No brand, game, film, show or franchise names (Genshin Impact, Lego, Pac-Man, Minecraft blocks such as Purpur, Poppy Playtime, The Shining, Neighbours From Hell, Backrooms, Deus Ex, Hunter x Hunter, Pokemon, Heretic): describe what the material looks like instead, and flag "trademark". Real places, cultures and artists are fine (Alhambra, Moroccan, Paris Metro, Athos Bulcao), use plain ASCII letters.
> - Fix typos (Funiture, Uppholstery, Mosiac, Stylzied, Chesterfierld, Fossilified, Constuction).
> - Translate names that are not in English (Madera is Wood, Messing is Brass; keep only the English meaning of Chinese names).
> - Food materials name the food ("Watermelon Rind", "Milk Chocolate Tiles").
>
> Flags (an empty list when nothing applies), with a short note saying why when you flag:
> - "not-a-surface": the preview shows an object, scene, emblem, lettering or picture rather than a surface material you would paint onto a mesh.
> - "demo": a tutorial, example, utility, test or node demonstration rather than a finished material (e.g. "Flowmap Basics", "Water level node", "Add Cracks Tutorial Material", "UV Grid").
> - "no-preview": the preview is missing or black.
> - "trademark": the current name references a brand, game, film or show.
> - "unsure": you could not tell what the material is and the name is a guess.
> - "near-duplicate": the current name says it is a variant of another material (e.g. a "1K version", "v2", "Remake").
> The note is empty when there are no flags.
