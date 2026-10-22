#pragma once
#include "SADXGraphics.h"
#include "STH2005Graphics.h"
#include "DashTrail.h"
#include "EffectBalls.h"
#include "DashSpheres.h"
#include "SonicTrail.h"

/*
The graphics pools and effects that are the game's (the seam between the engine and SonicFantasy; GPU_Globals.h includes this file by name, and every game supplies its own): the pools of the format families it reads (the engine's SADXGraphics.h and STH2005Graphics.h) and the effects drawn after the entities.
The engine calls the four functions below (Game/GameGraphics.cpp): initGameEffects once when the Renderer is made, tickGameEffects at the end of every gameplay tick, drawGameEffects at the end of every drawn view, destroyGameGraphics when the Renderer is destroyed (the effects, then the game's pools).
*/
namespace Graphics
{
	inline DashTrail dashTrail;
	inline EffectBalls effectBalls;
	inline DashSpheres dashSpheres;
	inline SonicTrail sonicTrail;

	void initGameEffects();
	void tickGameEffects(float tickTime_);
	void drawGameEffects();
	void destroyGameGraphics();
}
