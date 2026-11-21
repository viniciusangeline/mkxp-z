uniform sampler2D texture;

uniform highp vec2 fxSize;   /* screen size in pixels */
uniform highp vec4 fxA;      /* amp(px), length(px), phase(rad), zoom */
uniform vec4 fxB;      /* tint rgb, tint strength */
uniform highp vec4 fxRect;   /* viewport rect in pixels: x, y, w, h */

varying highp vec2 v_texCoord;

void main()
{
	highp vec2 px = v_texCoord * fxSize;
	highp vec2 c = fxRect.xy + fxRect.zw * 0.5;

	/* zoom around viewport center, then horizontal sine wobble per row */
	highp vec2 p = c + (px - c) / max(fxA.w, 0.01);
	p.x += fxA.x * sin(p.y / max(fxA.y, 1.0) * 6.2831853 + fxA.z);

	/* never sample outside the viewport */
	p = clamp(p, fxRect.xy + vec2(0.5), fxRect.xy + fxRect.zw - vec2(0.5));

	vec4 frag = texture2D(texture, p / fxSize);
	frag.rgb = mix(frag.rgb, fxB.rgb, fxB.a);

	gl_FragColor = frag;
}
