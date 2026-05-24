// requires:
// transform component
// mesh component (defines boundary, can rendered or not to debug)
// layer, allows to only collide and check for other colliders on the same layer

// functions:
// on collision, triggers when something has collided, stays true as long as is colliding 
// out of collision, triggers when the object just got out of collision

//has a predifined primitive shapes that are optimised.
// Cube, check for intersecting area
// sphere, check for radius
// capsule, 2 points, calculate the intersection with the thickness starting from the vector line
// cylinder, 2 points, vector with thickness
// custom mesh, TODO: requires a special algorithm.