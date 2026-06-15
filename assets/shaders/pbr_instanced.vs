#version 330 core
// PBR vertex shader (instanced). Identical to pbr.vs but the model matrix arrives per-instance via the
// mat4 attribute at locations 7-10 (filled from the renderer's instance buffer, divisor 1) instead of a
// uniform. Pair with pbr.fs.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 7) in mat4 aInstanceModel;

out vec3 FragPos;
out vec3 Normal;
out vec3 Tangent;
out vec2 TexCoords;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos   = vec3(aInstanceModel * vec4(aPos, 1.0));
    mat3 nrm  = transpose(inverse(mat3(aInstanceModel)));
    Normal    = nrm * aNormal;
    Tangent   = nrm * aTangent;
    TexCoords = aTexCoords;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
