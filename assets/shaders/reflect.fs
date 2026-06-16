#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 WorldPos;

uniform vec3 viewPos;        // camera world position
uniform samplerCube skybox;  // environment cube map to reflect (sRGB internal -> linear sample)

void main()
{
    // Mirror reflection of the environment: reflect the view direction about the surface normal
    // and look the result up in the cube map. The cube faces use an sRGB internal format, so the
    // sample is LINEAR here; the present pass applies exposure -> tonemap -> gamma like everything else.
    vec3 I = normalize(WorldPos - viewPos);
    vec3 R = reflect(I, normalize(Normal));
    FragColor = vec4(texture(skybox, R).rgb, 1.0);
}
