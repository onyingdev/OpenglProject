#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;   // camera view with translation stripped (mat3 -> mat4)

void main()
{
    TexCoords = aPos;                               // cube position doubles as the sample direction
    vec4 pos = projection * view * vec4(aPos, 1.0);
    gl_Position = pos.xyww;                          // force depth = 1.0 so the skybox sits behind everything
}
