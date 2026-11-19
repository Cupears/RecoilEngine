#version 310 es
precision highp float;

in vec3 vuvw;
in vec4 vColor;
out vec4 outColor;

uniform sampler2D colorTexture;
uniform sampler2D materialTexture;
uniform vec3 teamColor;
uniform bool drawTerrain;

void main()
{
	if (drawTerrain) {
		vec3 normal = normalize(vColor.rgb);
		float sun = max(dot(normal, normalize(vec3(-0.65, 0.75, -0.3))), 0.0);
		outColor = vec4(texture(colorTexture, vuvw.xy).rgb * (0.52 + 0.55 * sun), 1.0);
		return;
	}
	vec4 material = texture(materialTexture, vuvw.xy);
	if (material.a < 0.5)
		discard;

	vec4 base = texture(colorTexture, vuvw.xy);
	vec3 albedo = mix(base.rgb, teamColor, base.a);
	// The probe passes the mesh normal in the icon shader's color attribute.
	vec3 normal = normalize(vColor.rgb);
	float key = max(dot(normal, normalize(vec3(-0.4, 0.8, -0.5))), 0.0);
	float sky = 0.5 + 0.5 * normal.y;
	vec3 fill = mix(vec3(0.045, 0.040, 0.035), vec3(0.14, 0.18, 0.24), sky);
	vec3 light = albedo * (0.65 + 0.7 * key);
	// Approximate the reflected light that BAR normally gets from its environment map.
	light += fill * (0.32 + 0.68 * material.g) * (1.0 - base.a);
	light += vec3(0.32, 0.13, 0.04) * material.r;
	outColor = vec4(light, 1.0);
}
