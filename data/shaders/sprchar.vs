#version 420

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_texture;

uniform mat4 projectionMatrix;
uniform mat4 cameraMatrix;
uniform mat4 modelMatrix;
uniform float depthBias = 0.0;	// moves the sprite towards the camera so its feet are not cut off by the ground

out vec2 texCoord;

void main()
{
	texCoord = a_texture;
	vec4 v = cameraMatrix * modelMatrix * vec4(0.0, 0.0, 0.0, 1.0);
	v.xy += a_position.xy;
	v.z += depthBias;
	gl_Position = projectionMatrix * v;
}
