#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube skybox;

void main()
{
    // The cubemap faces use an sRGB internal format, so the sample is already LINEAR here.
    // We write linear into the HDR scene buffer; the present pass applies exposure -> tonemap
    // -> gamma, exactly like the lit geometry, so the background stays consistent with the scene.
    FragColor = vec4(texture(skybox, TexCoords).rgb, 1.0);
}
