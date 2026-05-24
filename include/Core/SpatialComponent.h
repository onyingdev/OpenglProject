#pragma once
#include "../Core/Component.h"
#include "../Components/TransformComponent3D.h"

//spatial its a type of component that has a local transform.
//as a general rule, a component that can be placed on the world is a spatial component.
class SpatialComponent : public Component {
public:
    // Per-axis source toggle: 
    enum class Source { Owner, Local };

    TransformComponent localTransform = TransformComponent(); 
    Source PositionSource = Source::Local; //by default uses the local
    Source RotationSource = Source::Local;
    Source ScaleSource    = Source::Local;
};

//TODO: optimise spatial component to save memory, since it uses TransformComponent3D it 
//has some redundant data, we can make a spatial component that only has the local transform and the source.
