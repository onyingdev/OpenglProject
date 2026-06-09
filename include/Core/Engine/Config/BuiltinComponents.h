#pragma once
//
// Registration manifest. Including this header pulls in every built-in component
// and game script, which triggers their REGISTER_COMPONENT self-registration. Keep
// this list in one linked translation unit (currently main.cpp) so the registry is
// fully populated no matter what else does or doesn't include these headers.
//
// This is the single place that knows the full set of registerable types. It mixes
// engine components and game scripts on purpose — it's the app's composition list;
// the loader/registry stay decoupled and never need editing when a type is added.
// When a new component/script gains a REGISTER_COMPONENT, add its header here.

// when adding a new component, register it here important!

// --- Engine components ---
#include "Core/Components/TransformComponent3D.h"
#include "Components/Rendering/MeshComponent.h"
#include "Components/Rendering/CameraComponent.h"
#include "Components/Rendering/PointLightComponent.h"
#include "Components/Rendering/ConeLightComponent.h"
#include "Components/Rendering/DirectionalLightComponent.h"
#include "Components/Input/InputComponent.h"
#include "Components/Physics/ColliderComponent3D.h"
#include "Components/Physics/RigidBodyComponent3D.h"
#include "Components/Gameplay/CharacterComponent3D.h"

// --- Game scripts ---
#include "assets/scripts/characterController.h"
#include "assets/scripts/spinModel.h"
#include "assets/scripts/cameraFollow.h"
#include "assets/scripts/cameraController.h"
#include "assets/scripts/flashLight.h"

//Add new scripts here and register them with the macro at the end of their header file.
//TODO: We need to decouple scripts entirely, components having a manual integration is not a problem, since its rare that we will need to add one. But not scripts, so refactor it to treat
// scripts differently.