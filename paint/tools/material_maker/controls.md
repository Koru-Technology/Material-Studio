# Material controls

A material shows a short list of controls a casual user can change without the node editor: the common controls, which
every material has, then the material's own controls (`controls.json`), at most six, chosen for each material of the
library. A control changes parameters of the material's nodes; the graph stays the same and stays editable in the node
editor.

## Control types

Every control has an `id` (snake_case, unique in the material), a `label` (Title Case, one to three plain words, no node
jargon: "Brick Rows", "Moss Amount", "Mortar Color"), a `type`, a `group` and `targets`: the node parameters it sets.
A target names a node by its path (its name at the top level of the graph, `graph/node` inside an inline graph node) and
one of its parameters.

| type | value | shows as | targets |
|---|---|---|---|
| `slider` | a number, `min` .. `max`, `default`, optional `step` (1 for counts) and `unit` | a slider | float parameters. Each target is set to the value, or mapped linearly with `to: [at min, at max]` |
| `toggle` | on or off, `default` | a switch | boolean parameters set to the value, or any parameters with `off` and `on` values |
| `choice` | one of 2 to 6 `options`, each with a `label`, `default` the label of the one in use | a segmented control (a menu past 3 options) | each option sets its targets to its `values`, one per target; enum parameters take the index of the option they name |
| `color` | a color, `default` as `#rrggbb` | a swatch | color parameters, or one stop of a gradient parameter (`stop`: its index) |
| `gradient` | the colors of a gradient's stops, positions fixed | a row of swatches | one gradient parameter |
| `seed` | a random seed | a Shuffle button | the seeds of the nodes it names, or of every node when it names none |

`unit`, for sliders: `percent` (the value is a fraction shown as a percentage: 0.5 shows as 50 %), `degrees`, `count`
(whole numbers), `multiplier` (shown as 1.5x), or none (shown as the number).

Groups, in this order: `color`, `pattern` (layout, size and shape of the elements: bricks, tiles, planks, cells),
`surface` (relief, wear, cracks, dirt, moss, rust, puddles), `detail` (finer noise, variation between elements).

A material control never repeats a common one. It may be a narrower version of one: the hue of the whole material is
common, the color of the moss or of the mortar is the material's own.

Parameters that change the cost or the resolution of the render (buffer size, quality, iterations of a blur, grid size)
are never controls.

## Common controls

Every material has these, applied after the material's own graph (adjustment nodes between it and the output, and the
Material node's parameters), so they work the same on every material and preview without compiling it again.

| control | group | type | range (default) | does |
|---|---|---|---|---|
| Hue | color | slider, degrees | -180 .. 180 (0) | turns the hue of the base color |
| Saturation | color | slider, percent | 0 .. 2 (1) | grays out or strengthens the base color |
| Brightness | color | slider, percent | 0 .. 2 (1) | darkens or lightens the base color |
| Roughness | surface | slider, percent | -1 .. 1 (0) | glossier to rougher: shifts the roughness, kept in 0 .. 1 |
| Metallic | surface | slider, percent | -1 .. 1 (0) | shifts the metallic, kept in 0 .. 1 |
| Bumps | surface | slider, percent | 0 .. 2 (1) | strength of the normal map |
| Height | surface | slider, percent | 0 .. 2 (1) | depth of the relief (displacement, parallax) |
| Glow | color | slider, percent | 0 .. 4 (1) | strength of the emission; only on materials that emit |
| Shuffle | detail | seed | | a new random variation of every random node |

Scale and rotation of the whole material on the surface are not controls: they are the material's UV Scale and UV
Rotation (the header, for paint layers) and a fill layer's own scale and angle.

They are made at import (`mm_controls_common`, `mm_controls.c`): Variables with an order below 0, so they come first in
their groups, wired into a Hue/Saturation/Value node (Hue through a Map Range from degrees), Math nodes (Roughness and
Metallic added and clamped, Height and Glow multiplied) and a Normal Map node (Bumps as its strength) between the
Material node and the output. The Studio previews them live (`parser_material_live_variable`). Shuffle drives no node:
its value is Material Maker's `seed_variation` in the material's shader and buffer passes.

## controls.json

`controls.json` holds the material's own controls, by library id. Each entry: `controls`, the list in the order shown
(most useful first), and `notes`, why some obvious control was left out, when one was. A material with nothing worth
exposing has an empty list: the common controls are enough.

```json
"30": {
 "controls": [
  {"id": "brick_rows", "label": "Brick Rows", "type": "slider", "group": "pattern", "min": 2, "max": 24, "default": 8,
   "step": 1, "unit": "count", "targets": [{"node": "bricks", "param": "rows"}, {"node": "bricks", "param": "columns", "to": [1, 12]}]},
  {"id": "mortar_width", "label": "Mortar Width", "type": "slider", "group": "pattern", "min": 0, "max": 0.05,
   "default": 0.003, "targets": [{"node": "bricks", "param": "mortar"}]},
  {"id": "mortar_color", "label": "Mortar Color", "type": "color", "group": "color", "default": "#8a8378",
   "targets": [{"node": "colorize", "param": "gradient", "stop": 0}]}
 ],
 "notes": ""
}
```

The defaults are the values in the material's graph, so a material shows as it always did until a control moves.
Controls are checked against the graph (`curate_controls.py`): every target exists, its parameter has the control's type
(float for sliders, boolean or any for toggles, enum or any for choices, color or gradient for colors), the default is the
current value, and the range stays within the parameter's.

## In the app

- **Variables.** A control is a Variable node (`nodes_material/variable_node.c`). It has a type (Number, Whole Number,
  Toggle, Choice, Color, Gradient), a default value, a range or options, a group and a unit, and its output is wired
  into the graph.
- **Parameter sockets.** A Material Maker parameter takes a Variable through a parameter socket: an input the parameter
  gets while it is wired (`mm_nodes.c`), named after the parameter, or `gradient#2` for one stop.
  - Before a shader is made, `variable_propagate` evaluates what each parameter socket receives and writes it into the
    parameter.
  - A `to` mapping is a Map Range node between the Variable and the parameter.
  - A choice whose targets take other values is a Select Value node.
- **Import.** `build_controls.py` builds `paint/assets/material_maker/controls.json`. `mm_controls_apply`
  (`nodes_material/mm_controls.c`) adds its Variables when a library material is imported. Targets inside inline graph
  nodes and on the Material node are not wired yet.
- **Material Studio.** A material with Variables opens in the Material Studio, which shows a control for each Variable
  by group (`ui/ui_material_studio.c`, Variables mode).
- **Node editor.** "Export Parameter..." in a Material Maker node's context menu adds a Variable for one of its
  parameters, or for a stop of a gradient.
- **Tests.** `paint/tests/material_controls.c`.
