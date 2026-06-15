#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;

void main()
{
    vec3 col = texture(screenTexture, TexCoords).rgb;
    //float grayscale = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
    //FragColor = vec4(vec3(grayscale), 1.0);

    // The scene is rendered + lit in LINEAR space (PBR); re-encode to sRGB gamma here, once, for display.
    // (Pairs with sRGB-internal-format color textures and linearized light/unlit colors.)
    col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);

}

