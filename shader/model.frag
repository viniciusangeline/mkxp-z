// Fragment shader for the Model class: unlit, nearest-filtered, alpha cutout.

uniform sampler2D texture;
uniform vec4 matColor;     // flat color used when the material has no texture
uniform float useTexture;  // 1.0 = sample texture, 0.0 = flat color

varying vec2 v_texCoord;

void main()
{
	vec4 frag = (useTexture > 0.5) ? texture2D(texture, v_texCoord) : matColor;

	if (frag.a < 0.5)
		discard;

	gl_FragColor = vec4(frag.rgb, 1.0);
}
