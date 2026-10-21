#pragma once
#include "SAInstance.h"
#include "ONEInstance.h"
#include "SALevelInstance.h"
#include "ONELevelInstance.h"
#include "ONE.h"
#include "DashTrail.h"
#include "EffectBalls.h"
#include "DashSpheres.h"
#include "SonicTrail.h"
#include "Pool.h"
#include "SAMesh.h"
#include "SAAnimation.h"
#include "TextureSet.h"
#include "TextureBinding.h"
#include "PoolMap.h"
#include <string>
#include <unordered_map>

/*
The graphics pools and effects that are the game's (the seam between the engine and SonicFantasy; GPU_Globals.h includes this file by name, and every game supplies its own): the SADX and Shadow meshes, motions and instances, and the effects drawn after the entities.
The engine calls the four functions below (Game/GameGraphics.cpp): initGameEffects once when the Renderer is made, tickGameEffects at the end of every gameplay tick, drawGameEffects at the end of every drawn view, destroyGameGraphics when the Renderer is destroyed (the effects, then the game's pools).
*/
namespace Graphics
{
	inline DashTrail dashTrail;
	inline EffectBalls effectBalls;
	inline DashSpheres dashSpheres;
	inline SonicTrail sonicTrail;
	inline PoolMap<TextureSet> textureSets = ("TextureSets");
	inline Pool<TextureBinding> textureBindings = ("TextureBindings");
	inline PoolMap<SAMesh> saMeshes = ("SAMeshes");
	inline std::unordered_map<std::string, RelModule> relModules;
	inline PoolMap<SAMotion> saMotions = ("SAMotions");
	inline Pool<SAInstance> saInstances = ("SAInstances"); 
	inline PoolMap<STH2005::ONE> oneArchives = ("ONEArchives");
	inline Pool<ONEInstance> oneInstances = ("ONEInstances");
	inline Pool<SALevelInstance> saLevelInstances = ("SALevelInstances");
	inline Pool<ONELevelInstance> oneLevelInstances = ("ONELevelInstances");

	void initGameEffects();
	void tickGameEffects(float tickTime_);
	void drawGameEffects();
	void destroyGameGraphics();
}
