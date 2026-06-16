#version 330 core
// FXAA present-pass fragment shader. Runs as the renderer's present pass when
// RenderSettings::aa == AntiAliasing::FXAA, sampling the single-sample scene
// target (screenTexture, bound to unit 0) and resolving edges in screen space.
//
// Ported from the classic geeks3d / webgl-meincraft FXAA (Armin Ronacher, 2011)
// to GLSL 330 core. The neighbour texcoords arrive precomputed from the vertex
// stage to avoid dependent texture reads.
out vec4 FragColor;

in vec2 TexCoords;
in vec2 v_rgbNW;
in vec2 v_rgbNE;
in vec2 v_rgbSW;
in vec2 v_rgbSE;
in vec2 v_rgbM;

uniform sampler2D screenTexture;
uniform vec2 iResolution;
uniform float gamma;     // present-pass output gamma (from render.json; ~2.2)
uniform float exposure;  // linear exposure multiplier applied before tonemapping
uniform int   tonemap;   // 0=None 1=Reinhard 2=ACES (RenderSettings::TonemapMode)

#ifndef FXAA_REDUCE_MIN
    #define FXAA_REDUCE_MIN   (1.0 / 128.0)
#endif
#ifndef FXAA_REDUCE_MUL
    #define FXAA_REDUCE_MUL   (1.0 / 8.0)
#endif
#ifndef FXAA_SPAN_MAX
    #define FXAA_SPAN_MAX     8.0
#endif

vec4 fxaa(sampler2D tex, vec2 fragCoord, vec2 resolution,
          vec2 v_rgbNW, vec2 v_rgbNE,
          vec2 v_rgbSW, vec2 v_rgbSE,
          vec2 v_rgbM) {
    vec4 color;
    vec2 inverseVP = vec2(1.0 / resolution.x, 1.0 / resolution.y);
    vec3 rgbNW = texture(tex, v_rgbNW).xyz;
    vec3 rgbNE = texture(tex, v_rgbNE).xyz;
    vec3 rgbSW = texture(tex, v_rgbSW).xyz;
    vec3 rgbSE = texture(tex, v_rgbSE).xyz;
    vec4 texColor = texture(tex, v_rgbM);
    vec3 rgbM  = texColor.xyz;
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) *
                          (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(FXAA_SPAN_MAX, FXAA_SPAN_MAX),
              max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
              dir * rcpDirMin)) * inverseVP;

    vec3 rgbA = 0.5 * (
        texture(tex, fragCoord * inverseVP + dir * (1.0 / 3.0 - 0.5)).xyz +
        texture(tex, fragCoord * inverseVP + dir * (2.0 / 3.0 - 0.5)).xyz);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(tex, fragCoord * inverseVP + dir * -0.5).xyz +
        texture(tex, fragCoord * inverseVP + dir *  0.5).xyz);

    float lumaB = dot(rgbB, luma);
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
        color = vec4(rgbA, texColor.a);
    else
        color = vec4(rgbB, texColor.a);
    return color;
}

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

void main() {
    vec2 fragCoord = TexCoords * iResolution;
    FragColor = fxaa(screenTexture, fragCoord, iResolution,
                     v_rgbNW, v_rgbNE, v_rgbSW, v_rgbSE, v_rgbM);

    // Final present pass: scene was lit in LINEAR HDR (PBR). FXAA resolves on the linear buffer
    // (edge luma stays monotonic), then expose -> tonemap -> gamma-encode the result.
    FragColor.rgb *= exposure;
    FragColor.rgb = applyTonemap(FragColor.rgb);
    FragColor.rgb = pow(FragColor.rgb, vec3(1.0 / gamma));

    // Triangular-PDF dither (~1 LSB) to break up residual banding at the 8-bit output quantization.
    float dt = (hash12(gl_FragCoord.xy) + hash12(gl_FragCoord.xy + 17.0) - 1.0) / 255.0;
    FragColor.rgb += dt;
}
