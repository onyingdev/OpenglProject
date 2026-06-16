#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform float gamma;     // present-pass output gamma (from render.json; ~2.2)
uniform float exposure;  // linear exposure multiplier applied before tonemapping
uniform int   tonemap;   // 0=None 1=Reinhard 2=ACES (RenderSettings::TonemapMode)

float hash12(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// Narkowicz 2015 ACES filmic approximation; input/output linear.
vec3 acesFilmic(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Map HDR linear radiance into [0,1]. Done BEFORE the gamma encode.
vec3 applyTonemap(vec3 c) {
    if (tonemap == 1)      return c / (c + vec3(1.0));   // Reinhard
    else if (tonemap == 2) return acesFilmic(c);          // ACES
    return clamp(c, 0.0, 1.0);                            // None
}

void main()
{
    vec3 col = texture(screenTexture, TexCoords).rgb;
    //float grayscale = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
    //FragColor = vec4(vec3(grayscale), 1.0);

    // The scene is rendered + lit in LINEAR HDR (PBR). Expose -> tonemap -> gamma-encode, once.
    // (Pairs with sRGB-internal-format color textures and linearized light/unlit colors.)
    col *= exposure;
    col = applyTonemap(col);
    col = pow(col, vec3(1.0 / gamma));

    // Triangular-PDF dither (~1 LSB) to break up any residual banding at the 8-bit output quantization.
    float dt = (hash12(gl_FragCoord.xy) + hash12(gl_FragCoord.xy + 17.0) - 1.0) / 255.0;
    FragColor = vec4(col + dt, 1.0);
}

