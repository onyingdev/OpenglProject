#version 330 core
// Shared vertex stage for the IBL bakes (irradiance + prefilter). Renders a unit cube from the 6
// capture views; the local cube position doubles as the world-space sample direction in the fs.
layout (location = 0) in vec3 aPos;

out vec3 localPos;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    localPos = aPos;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
