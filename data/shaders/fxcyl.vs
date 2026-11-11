#version 420

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_texture;

uniform mat4 projectionMatrix;
uniform mat4 cameraMatrix;
uniform mat4 modelMatrix;

out vec2 texCoord;

void main()
{
	texCoord = a_texture;
	gl_Position = projectionMatrix * cameraMatrix * modelMatrix * vec4(a_position, 1.0);
}
