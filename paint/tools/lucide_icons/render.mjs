// Renders the Lucide icons listed in icons.json to white PNG tiles.
// Usage: node render.mjs <lucide-static/icons dir> <out dir>   (needs @resvg/resvg-js)
import { Resvg } from "@resvg/resvg-js";
import fs from "fs";
import path from "path";

const [iconsDir, outDir] = process.argv.slice(2);
const here = path.dirname(new URL(import.meta.url).pathname);
const map = JSON.parse(fs.readFileSync(path.join(here, "icons.json"), "utf8"));
fs.mkdirSync(outDir, { recursive: true });

for (const [slot, entry] of Object.entries(map)) {
	if (slot.startsWith("_")) continue;
	const opt = typeof entry === "string" ? { name: entry } : entry;
	let svg = fs.readFileSync(path.join(iconsDir, opt.name + ".svg"), "utf8");
	svg = svg.replace(/currentColor/g, "#ffffff");
	if (opt.fill) svg = svg.replace(/fill="none"/, 'fill="#ffffff"');
	if (opt.rotate) svg = svg.replace(/(<svg[^>]*>)([\s\S]*)(<\/svg>)/, `$1<g transform="rotate(${opt.rotate} 12 12)">$2</g>$3`);
	// Atlas tiles are 25 / 50 / 100 px (icons05x / icons / icons2x), the eye strip 9 / 18 / 36 px
	const sizes = slot.startsWith("eye") ? [9, 18, 36] : [25, 50, 100];
	for (const s of sizes) {
		const px = slot.startsWith("eye") ? s : Math.round(s * 0.84);
		const png = new Resvg(svg, { fitTo: { mode: "width", value: px } }).render().asPng();
		fs.writeFileSync(path.join(outDir, `${slot}_${s}.png`), png);
	}
}
