#version 300 es
precision highp float;

uniform sampler2D tex;

in vec2 texture_coords;
in vec4 color;

layout (location = 0) out vec4 frag_color;

void main ()
{
	frag_color = texture2D (tex, texture_coords) * color;
}
