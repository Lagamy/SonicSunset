#pragma once
#include "SAMesh.h"

/*
What the engine's collision code takes from the game for shapes that follow the SADX conventions (usesNinjaShapes in GameEntity.h; the seam between the engine and SonicFantasy, every game supplies its own): Graphics::composeNinjaTransform and the Ninja flags
(NinjaNoPosition, NinjaNoScale), which turn a shape by its Ninja angles.
*/
