#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float opac;
	layout(offset = 4) int blending;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D texa;

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {	vout.tex = in_pos.xy * 0.5 + 0.5;
	vout.tex.y = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	float col0 = textureLod(sampler2D(tex0, sampler_linear), vin.tex, 0.0).r;
	float cola = textureLod(sampler2D(texa, sampler_linear), vin.tex, 0.0).r;
	float str = constants.opac;
	float out_color = 0.0;

	if (constants.blending == 0) { // Mix
		out_color = mix(cola, col0, str);
	}
	/*else*/ if (constants.blending == 1) { // Darken
		out_color = mix(cola, min(cola, col0), str);
	}
	/*else*/ if (constants.blending == 2) { // Multiply
		out_color = mix(cola, cola * col0, str);
	}
	/*else*/ if (constants.blending == 3) { // Burn
		out_color = mix(cola, 1.0 - (1.0 - cola) / col0, str);
	}
	/*else*/ if (constants.blending == 4) { // Lighten
		out_color = max(cola, col0 * str);
	}
	/*else*/ if (constants.blending == 5) { // Screen
		out_color = (1.0 - ((1.0 - str) + str * (1.0 - col0)) * (1.0 - cola));
	}
	/*else*/ if (constants.blending == 6) { // Dodge
		out_color = mix(cola, cola / (1.0 - col0), str);
	}
	/*else*/ if (constants.blending == 7) { // Add
		out_color = mix(cola, cola + col0, str);
	}
	/*else*/ if (constants.blending == 8) { // Overlay
		// out_color = mix(cola, cola < 0.5 ? 2.0 * cola * col0 : 1.0 - 2.0 * (1.0 - cola) * (1.0 - col0), str);

		////
		//if (cola < 0.5) {
			out_color = mix(cola, 2.0 * cola * col0, str);
		//}
		//else {
		//	out_color = mix(cola, 1.0 - 2.0 * (1.0 - cola) * (1.0 - col0), str);
		//}
		////
	}
	/*else*/ if (constants.blending == 9) { // Soft Light
		out_color = ((1.0 - str) * cola + str * ((1.0 - cola) * col0 * cola + cola * (1.0 - (1.0 - col0) * (1.0 - cola))));
	}
	/*else*/ if (constants.blending == 10) { // Linear Light
		out_color = (cola + str * (2.0 * (col0 - 0.5)));
	}
	/*else*/ if (constants.blending == 11) { // Difference
		out_color = mix(cola, abs(cola - col0), str);
	}
	/*else*/ if (constants.blending == 12) { // Subtract
		out_color = mix(cola, cola - col0, str);
	}
	/*else*/ if (constants.blending == 13) { // Divide
		out_color = (1.0 - str) * cola + str * cola / col0;
	}
	/*else*/ if (constants.blending == 14) { // Hue, Saturation, Color, Value
		out_color = mix(cola, col0, str);
	}

	frag_color = vec4(out_color, out_color, out_color, 1.0);
}
#endif
