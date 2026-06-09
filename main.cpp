#include "Core/Engine/Engine.h"

// Everything lives behind Engine now — window, renderer, input, the frame loop and the
// SceneManager. Engine reads its config from app/ (engine/render, input, root scene).
int main()
{
    Engine engine;
    if (!engine.init()) return -1;
    engine.run();
    return 0;
}
