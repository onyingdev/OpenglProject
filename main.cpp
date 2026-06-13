#include "Core/Engine/Engine.h"
//Will give an error when executing directly from desktop. The .exe must be next to the files
//since it uses relative path. It can be fix with a build game command that builds the 
//whole game.
//TODO: Add make build, to do just that and exports all the files necessary. Also add the .dll and .so for linux

// Everything lives behind Engine now — window, renderer, input, the frame loop and the
// SceneManager. Engine reads its config from app/ (engine/render, input, root scene).
int main()
{
    Engine engine;
    if (!engine.init()) return -1;
    engine.run();
    return 0;
}
