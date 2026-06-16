#version 330 core
// PBR vertex shader (single-draw). Model matrix via the `model` uniform. Outputs the world-space
// position, normal and tangent so the fragment shader can build a TBN frame for normal mapping and run
// Cook-Torrance lighting in world space. Tangent frame follows the LearnOpenGL normal-mapping reference.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;

out vec3 FragPos;
out vec3 Normal;
out vec3 Tangent;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos   = vec3(model * vec4(aPos, 1.0));
    mat3 nrm  = transpose(inverse(mat3(model)));   // normal matrix (handles non-uniform scale)
    Normal    = nrm * aNormal;
    Tangent   = nrm * aTangent;
    TexCoords = aTexCoords;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
