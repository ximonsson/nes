#version 300 es

in vec3 vertex;
in vec2 texture_coords_in;
in vec4 color_in;

out vec2 texture_coords;
out vec4 color;

void main ()
{
	gl_Position    = vec4 (vertex, 1.0);
	texture_coords = texture_coords_in;
	color          = color_in;
}
