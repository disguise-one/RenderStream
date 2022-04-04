#version 450
layout(location = 0) in vec3 vert;

layout(binding = 0) uniform UBO {
	mat4 WVP;
} ubo;

void main() { 
	gl_Position = ubo.WVP * vec4( vert, 1 ); 
}
