#pragma once

class InputManager;

// Runtime services a GameObject needs at load time but that are NOT stored in the
// file (e.g. the InputManager an InputComponent binds to). This grows later with an
// asset cache and a name->object map for cross-object reference resolution.
struct LoadContext {
    InputManager* input = nullptr;
};
