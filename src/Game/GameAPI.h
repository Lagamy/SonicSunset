#pragma once
#include "PoolId.h"
#include "GameKeys.h"

/* The part of the API (API.h, which includes this file by name) that is the game's: the motion an entity wants. Defined in Game/GameAPI.cpp. */

void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_);
void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_, const SAModelKey& rMesh_, bool loop_ = true);
void changeAnimation(PoolId entityId_, const STH2005::MotionKey& rKey_, bool loop_ = true);
void blinkAnimation(PoolId entityId_, const SAMotionKey* pKey_, const SAModelKey* pMesh_);
