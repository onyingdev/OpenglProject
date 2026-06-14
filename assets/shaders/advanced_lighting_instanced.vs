#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
// Per-instance model matrix (one mat4 spans four attribute slots: 7,8,9,10), divisor 1.
// Filled from the renderer's instance buffer; replaces the per-draw `uniform mat4 model`.
layout (location = 7) in mat4 aInstanceModel;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos = vec3(aInstanceModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(aInstanceModel))) * aNormal;
    TexCoords = aTexCoords;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
