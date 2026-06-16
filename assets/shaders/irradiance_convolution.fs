#version 330 core
// Diffuse IBL bake: convolve the environment cube map into an irradiance map. Each output texel is the
// cosine-weighted integral of incoming radiance over the hemisphere around its direction, so the lit
// shader can read diffuse ambient with a single texture(irradianceMap, N). (Karis split-sum, diffuse term.)
out vec4 FragColor;

in vec3 localPos;

uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(localPos);

    // Tangent frame around N for hemisphere sampling.
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    vec3 irradiance = vec3(0.0);
    float sampleDelta = 0.025;
    float nrSamples   = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // spherical -> cartesian (tangent space), then -> world
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / nrSamples);
    FragColor = vec4(irradiance, 1.0);
}
