precision mediump float;

uniform sampler2D tex;
varying vec2 texture_coords;

/**
 *	NES color palette
 */
vec3 palette[64] = vec3[64](
	vec3(3,3,3), vec3(0,1,4), vec3(0,0,6), vec3(3,2,6),
	vec3(4,0,3), vec3(5,0,3), vec3(5,1,0), vec3(4,2,0),
	vec3(3,2,0), vec3(1,2,0), vec3(0,3,1), vec3(0,4,0),
	vec3(0,2,2), vec3(0,0,0), vec3(0,0,0), vec3(0,0,0),
	vec3(5,5,5), vec3(0,3,6), vec3(0,2,7), vec3(4,0,7),
	vec3(5,0,7), vec3(7,0,4), vec3(7,0,0), vec3(6,3,0),
	vec3(4,3,0), vec3(1,4,0), vec3(0,4,0), vec3(0,5,3),
	vec3(0,4,4), vec3(0,0,0), vec3(0,0,0), vec3(0,0,0),
	vec3(7,7,7), vec3(3,5,7), vec3(4,4,7), vec3(6,3,7),
	vec3(7,0,7), vec3(7,3,7), vec3(7,4,0), vec3(7,5,0),
	vec3(6,6,0), vec3(3,6,0), vec3(0,7,0), vec3(2,7,6),
	vec3(0,7,7), vec3(4,4,4), vec3(0,0,0), vec3(0,0,0),
	vec3(7,7,7), vec3(5,6,7), vec3(6,5,7), vec3(7,5,7),
	vec3(7,4,7), vec3(7,5,5), vec3(7,6,4), vec3(7,7,2),
	vec3(7,7,3), vec3(5,7,2), vec3(4,7,3), vec3(2,7,6),
	vec3(4,6,7), vec3(6,6,6), vec3(0,0,0), vec3(0,0,0)
);

void main ()
{
	int c = int (texture2D (tex, texture_coords).a * 255.0);
	// the palette is 3-bit and we need to convert to 8-bit
	gl_FragColor = palette[c] / vec3 (7, 7, 7);
}
