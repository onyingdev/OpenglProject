#version 330 core
// FXAA present-pass vertex shader. Shares the renderer's fullscreen quad layout
// (see OpenGLRenderer::buildQuad): location 0 = NDC position, location 1 = UV.
//
// The four diagonal neighbour texcoords (NW/NE/SW/SE) and the centre (M) are
// computed here and passed down as varyings so the fragment shader avoids
// dependent texture reads. iResolution is the resolution of the texture being
// sampled (the offscreen scene/resolve target), not the window.
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;
out vec2 v_rgbNW;
out vec2 v_rgbNE;
out vec2 v_rgbSW;
out vec2 v_rgbSE;
out vec2 v_rgbM;

uniform vec2 iResolution;

void texcoords(vec2 fragCoord, vec2 resolution,
               out vec2 v_rgbNW, out vec2 v_rgbNE,
               out vec2 v_rgbSW, out vec2 v_rgbSE,
               out vec2 v_rgbM) {
    vec2 inverseVP = 1.0 / resolution.xy;
    v_rgbNW = (fragCoord + vec2(-1.0, -1.0)) * inverseVP;
    v_rgbNE = (fragCoord + vec2( 1.0, -1.0)) * inverseVP;
    v_rgbSW = (fragCoord + vec2(-1.0,  1.0)) * inverseVP;
    v_rgbSE = (fragCoord + vec2( 1.0,  1.0)) * inverseVP;
    v_rgbM  = fragCoord * inverseVP;
}

void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);

    vec2 fragCoord = aTexCoords * iResolution;
    texcoords(fragCoord, iResolution, v_rgbNW, v_rgbNE, v_rgbSW, v_rgbSE, v_rgbM);
}
