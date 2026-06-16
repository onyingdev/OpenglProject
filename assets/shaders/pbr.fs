#version 330 core
// PBR fragment shader — metallic-roughness, Cook-Torrance microfacet BRDF for analytic lights.
//   D = GGX/Trowbridge-Reitz (alpha = roughness^2, Disney remap)
//   G = Smith with Schlick-GGX, direct-light k = (roughness+1)^2 / 8 (Karis/UE4)
//   F = Schlick, F0 = mix(0.04, albedo, metallic)
// Diffuse is Lambert (albedo/PI), energy-conserved by kD = (1-F)(1-metallic). Output is LINEAR; the
// present pass applies the sRGB gamma encode. No IBL yet — ambient is a flat dirLight.ambient fill.
out vec4 FragColor;

struct DirLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
struct PointLight {
    vec3 position;
    float constant;
    float linear;
    float quadratic;
    float attenuation;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
struct SpotLight {
    vec3 position;
    vec3 direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

#define NR_POINT_LIGHTS 4
#define NR_SPOT_LIGHTS 4
const float PI = 3.14159265359;

in vec3 FragPos;
in vec3 Normal;
in vec3 Tangent;
in vec2 TexCoords;

// Bound by Mesh::bindTextures via the texture_<type>N convention. base color + emissive are sRGB
// (linearized on sample); mr/normal/occlusion are linear data. Missing maps get neutral defaults.
uniform sampler2D texture_diffuse1;    // base color (albedo), sRGB
uniform sampler2D texture_mr1;         // G = roughness, B = metallic
uniform sampler2D texture_normal1;     // tangent-space normal
uniform sampler2D texture_occlusion1;  // R = ambient occlusion
uniform sampler2D texture_emissive1;   // emissive, sRGB

// Per-mesh material factors (glTF). Each multiplies its map (glTF factors are authored linear).
uniform vec3  baseColor;
uniform float metallicFactor;
uniform float roughnessFactor;
uniform vec3  emissiveFactor;
uniform float occlusionStrength;
uniform float normalScale;

// glTF alpha. baseColorAlpha = baseColorFactor.a; alphaMode 0=OPAQUE / 1=MASK (cutout) / 2=BLEND.
// Alpha = baseColorAlpha * base-color map alpha. The renderer draws BLEND meshes in a separate blended
// pass; OPAQUE/MASK draw opaque (MASK discards below alphaCutoff). Output alpha matters only for BLEND.
uniform float baseColorAlpha;
uniform int   alphaMode;
uniform float alphaCutoff;
uniform int   doubleSided;   // 1 = flip the normal on back faces so both sides light correctly

uniform vec3 viewPos;
uniform DirLight dirLight;
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform SpotLight spotLights[NR_SPOT_LIGHTS];
uniform int numPointLights;
uniform int numSpotLights;

// Image-based lighting (Karis split-sum), baked once from the scene environment cube map:
//   irradianceMap = diffuse, prefilterMap = roughness-mipped specular, brdfLUT = environment BRDF.
// hasIBL == 0 (no skybox / IBL) falls back to the hemisphere ambient at the bottom of main().
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D   brdfLUT;
uniform int hasIBL;
const float MAX_REFLECTION_LOD = 4.0;   // prefilterMap has 5 mips (0..4 -> roughness 0..1)

// Per-environment IBL contribution multipliers (from the cubemap.json / skybox config). Let a cubemap act
// as "light information" (diffuse irradiance) and/or "reflection information" (specular) independently:
// lowering iblDiffuse hands diffuse shading back to the directional sun (less uniform), iblSpecular tunes
// the reflections. Default 1.0 (full IBL) — set by the renderer per (model,env) group.
uniform float iblDiffuse;
uniform float iblSpecular;

vec3 getNormal()
{
    vec3 N = normalize(Normal);
    // Double-sided materials are lit from both sides: on a back-facing fragment the interpolated normal
    // points away from the viewer, so flip it (and the TBN built from it below) to face us.
    if (doubleSided == 1 && !gl_FrontFacing) N = -N;
    // Re-orthonormalize the tangent against N (Gram-Schmidt). If there's no usable tangent frame
    // (asset without tangents/UVs -> Tangent ~ 0, or tangent parallel to N), skip normal mapping and
    // use the geometric normal — otherwise normalize() of a zero vector yields NaN and the surface
    // goes black. (The loader generates tangents when it can; this is the last-resort guard.)
    vec3 T0 = Tangent - dot(Tangent, N) * N;
    if (dot(T0, T0) < 1e-8) return N;
    vec3 T = normalize(T0);
    vec3 B = cross(N, T);

    vec3 n = texture(texture_normal1, TexCoords).rgb * 2.0 - 1.0;  // tangent space [-1,1]
    n.xy *= normalScale;                                          // glTF normalScale (xy only)
    return normalize(mat3(T, B, N) * n);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;     // Disney roughness remap (alpha = roughness^2)
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;              // direct-light remap (Karis) — NOT the IBL k=a^2/2
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with a roughness term for the IBL ambient (rough surfaces shouldn't show a sharp rim).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// One analytic light's contribution: Cook-Torrance specular + Lambert diffuse, weighted by radiance.
vec3 cookTorrance(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float roughness, float metallic, vec3 F0)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3  H = normalize(V + L);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  spec = (D * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);
    vec3  kD   = (vec3(1.0) - F) * (1.0 - metallic);   // no diffuse for metals
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

void main()
{
    vec4  baseSample = texture(texture_diffuse1, TexCoords);
    vec3  albedo     = baseColor * baseSample.rgb;
    float alpha      = baseColorAlpha * baseSample.a;
    if (alphaMode == 1 && alpha < alphaCutoff) discard;   // MASK cutout

    vec3  mr        = texture(texture_mr1, TexCoords).rgb;
    float metallic  = metallicFactor * mr.b;
    float roughness = clamp(roughnessFactor * mr.g, 0.04, 1.0);   // floor avoids a singular highlight
    float ao        = 1.0 + occlusionStrength * (texture(texture_occlusion1, TexCoords).r - 1.0);

    vec3 N = getNormal();
    vec3 V = normalize(viewPos - FragPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Directional "sun" — radiance is the light's (linear) diffuse color, no attenuation.
    Lo += cookTorrance(N, V, normalize(-dirLight.direction), dirLight.diffuse, albedo, roughness, metallic, F0);

    // Point lights.
    for (int i = 0; i < numPointLights; ++i) {
        vec3  L = normalize(pointLights[i].position - FragPos);
        float d = length(pointLights[i].position - FragPos);
        float att = pointLights[i].attenuation /
                    (pointLights[i].constant + pointLights[i].linear * d + pointLights[i].quadratic * d * d);
        Lo += cookTorrance(N, V, L, pointLights[i].diffuse * att, albedo, roughness, metallic, F0);
    }

    // Spot / cone lights.
    for (int i = 0; i < numSpotLights; ++i) {
        vec3  L = normalize(spotLights[i].position - FragPos);
        float d = length(spotLights[i].position - FragPos);
        float att = 1.0 / (spotLights[i].constant + spotLights[i].linear * d + spotLights[i].quadratic * d * d);
        float theta   = dot(L, normalize(-spotLights[i].direction));
        float epsilon = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float intensity = clamp((theta - spotLights[i].outerCutOff) / epsilon, 0.0, 1.0);
        Lo += cookTorrance(N, V, L, spotLights[i].diffuse * att * intensity, albedo, roughness, metallic, F0);
    }

    float NdotV = max(dot(N, V), 0.0);
    vec3  R     = reflect(-V, N);
    vec3  ambient;

    if (hasIBL == 1) {
        // Cube-map IBL (Karis split-sum). Diffuse = irradiance * albedo (energy-conserved by kD);
        // specular = prefiltered radiance * (F * brdf.scale + brdf.bias). Both modulated by AO.
        vec3 F  = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 ambDiffuse = irradiance * albedo;

        vec3 prefiltered = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
        vec2 envBRDF     = texture(brdfLUT, vec2(NdotV, roughness)).rg;
        vec3 ambSpecular = prefiltered * (F * envBRDF.x + envBRDF.y);

        // Scale the "environment as light" (diffuse) and "environment as reflection" (specular) terms
        // independently so a cubemap can be reflection-only / light-only / both.
        ambient = (kD * ambDiffuse * iblDiffuse + ambSpecular * iblSpecular) * ao;
    } else {
        // Hemisphere ambient fallback (no skybox/IBL): sky color = dirLight.ambient, ground darker.
        // Diffuse fill follows the up-facing normal (suppressed for metals); a crude Fresnel sky
        // reflection keeps dark/metallic surfaces from reading as black silhouettes.
        vec3 skyAmb    = dirLight.ambient;
        vec3 groundAmb = dirLight.ambient * 0.35;

        vec3 ambColorN  = mix(groundAmb, skyAmb, clamp(0.5 * N.y + 0.5, 0.0, 1.0));
        vec3 ambDiffuse = ambColorN * albedo * ao * (1.0 - metallic);

        vec3 ambColorR   = mix(groundAmb, skyAmb, clamp(0.5 * R.y + 0.5, 0.0, 1.0));
        vec3 Famb        = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 ambSpecular = ambColorR * Famb * ao;

        ambient = ambDiffuse + ambSpecular;
    }

    vec3 emissive = emissiveFactor * texture(texture_emissive1, TexCoords).rgb;

    // Output alpha matters only for the BLEND pass (blending is off for opaque/mask). linear; present
    // pass encodes gamma.
    FragColor = vec4(ambient + Lo + emissive, alphaMode == 2 ? alpha : 1.0);
}
