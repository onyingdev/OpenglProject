#version 330 core
// Fullscreen-quad vertex stage for the BRDF-LUT bake. Matches the renderer's present quad layout
// (location 0 = NDC xy, location 1 = uv), so it reuses the existing m_quadVao.
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main()
{
    TexCoords   = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
