#include "ScriptInternal.h"
#include "Scripting.h"
#include "API.h"
#include "Globals.h"
#include "Prefab.h"
#include "ResourceKeys.h"
#include "PlayerLoader.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "ONEInstance.h"
#include "lua.h"
#include "lauxlib.h"
#include <string>
#include <unordered_map>
#include <vector>

/*
The functions of the table `sf` that are the game's (the engine side is Scripting/ScriptAPI.cpp): the characters' motions and poses, the effects, and the spawning of the two players.
*/
namespace Scripting
{
/*
The motions an entity can be made to play by name: Sonic's (an SA entity: its motion and the model it is made for) and Shadow's (an STH2005 entity: its motion and whether it loops; Sonic's entries loop unless the file says "loop": false). Both tables are read from
Assets/External/Players/Sonic.json and Shadow.json (Loading::loadPlayerKeys). animate picks the Shadow table for an entity of the STH2005_ONE engine.
Harmless to call every tick: the entity plays a motion when it differs from the one playing.
*/
using Animation = Loading::SonicAnimation;
using ShadowAnimation = Loading::ShadowAnimation;

static const std::unordered_map<std::string, Animation>& animationTable()
{
	return Loading::sonicKeys().animations;
}

static const std::unordered_map<std::string, ShadowAnimation>& shadowAnimationTable()
{
	return Loading::shadowKeys().animations;
}

static int animate(lua_State* pLua_)
{
	const auto& animations = animationTable();
	PoolId id = checkEntity(pLua_, 1);
	std::string name = luaL_checkstring(pLua_, 2);
	if(Globals::Game::entities[id].sourceEngine == STH2005_ONE)
	{
		const auto& shadowAnimations = shadowAnimationTable();
		auto shadowFound = shadowAnimations.find(name);
		if(shadowFound == shadowAnimations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
		changeAnimation(id, *shadowFound->second.pMotion, shadowFound->second.loop);
		/* The optional third argument is the playback speed as a multiple of the 60 frames a second a motion plays at (0 or none = 1). */
		Globals::Game::entities[id].animationRate = static_cast<float>(luaL_optnumber(pLua_, 3, 0.0));
		return 0;
	}
	auto found = animations.find(name);
	if(found == animations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
	changeAnimation(id, *found->second.pMotion, *found->second.pMesh, found->second.loop);
	/* The optional third argument is the playback speed in motion frames per game frame (the game's own unit: external, FUN_804cf4f4 adds speed times a table value each game frame); 0 or none = the default rate, negative = backwards (an SA motion: from its last frame). */
	Globals::Game::entities[id].animationRate = static_cast<float>(luaL_optnumber(pLua_, 3, 0.0)) * Globals::Game::gameplaySystem.tickRate;
	/* The optional fourth argument is the blend step of the motion table entry (external, FUN_804cf4f4: the share of the way from the old pose to the new motion's first frame added each game frame): over 0 and under 1 the motion change blends, 0, 1 or none = a cut. */
	float blendStep = static_cast<float>(luaL_optnumber(pLua_, 4, 0.0));
	Globals::Game::entities[id].animationBlendRate = blendStep > 0.0f && blendStep < 1.0f ? blendStep * Globals::Game::gameplaySystem.tickRate : 0.0f;
	return 0;
}

/*
pose(e, {{name, frame, weight}, ...}): a Shadow entity's main skeleton shows these motions (the names of the table above) sampled at those frames, mixed by weight (ONEInstance::setPose); an empty table ends it. The timing is the script's own.
motionRange(e, name): the first and last frame the game plays that motion in (STH2005::Motion::start, end; most are 0 to the motion's length).
*/
static int pose(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TTABLE);
	const auto& shadowAnimations = shadowAnimationTable();
	std::vector<Graphics::PoseRequest> layers;
	for(lua_Integer i = 1; lua_geti(pLua_, 2, i) != LUA_TNIL; i++)
	{
		lua_geti(pLua_, -1, 1);
		std::string name = luaL_checkstring(pLua_, -1);
		lua_geti(pLua_, -2, 2);
		float frame = static_cast<float>(luaL_checknumber(pLua_, -1));
		lua_geti(pLua_, -3, 3);
		float weight = static_cast<float>(luaL_checknumber(pLua_, -1));
		lua_pop(pLua_, 4);
		auto found = shadowAnimations.find(name);
		if(found == shadowAnimations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
		layers.push_back(Graphics::PoseRequest{found->second.pMotion, frame, weight});
	}
	lua_pop(pLua_, 1);
	Globals::Game::entities[id].setONEPose(layers);
	return 0;
}

/* setAnimationScaleMode(n): Graphics::ONEScaleMode, a debug switch for what the motions' scale channels do (0 scale and compensation, 1 neither, 2 scale only). */
static int setAnimationScaleMode(lua_State* pLua_)
{
	Graphics::ONEScaleMode = static_cast<int>(luaL_checkinteger(pLua_, 1));
	return 0;
}

/* setAnimationEulerOrder(n): Graphics::ONEEulerOrder, a debug switch for the order the Euler angles of a motion are applied in (0 to 5). */
static int setAnimationEulerOrder(lua_State* pLua_)
{
	Graphics::ONEEulerOrder = static_cast<int>(luaL_checkinteger(pLua_, 1));
	return 0;
}

/*
bone(e, name): for the debug log, a frame of a Shadow entity's skeleton as it was posed last: local translation x y z, local rotation as Euler angles in radians (Rz * Ry * Rx) x y z, scale x y z, and the position in the model's own space x y z (12 numbers);
nothing if the skeleton has no frame of that name.
*/
static int bone(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	std::string name = luaL_checkstring(pLua_, 2);
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.sourceEngine != STH2005_ONE || rEntity.instanceId == UninitializedPoolId) return luaL_error(pLua_, "entity %I has no ONE instance", toHandle(id));
	float values[12];
	if(!Graphics::getONEInstance(rEntity.instanceId).boneInfo(name, values)) return 0;
	for(float value : values) lua_pushnumber(pLua_, value);
	return 12;
}

/*
lowestPoint(e): for the debug log, the lowest y of a Shadow entity's skinned vertices in the model's own space: as posed last, then in the rest pose (2 numbers; the sole against the model's origin).
*/
static int lowestPoint(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.sourceEngine != STH2005_ONE || rEntity.instanceId == UninitializedPoolId) return luaL_error(pLua_, "entity %I has no ONE instance", toHandle(id));
	float values[2];
	Graphics::getONEInstance(rEntity.instanceId).lowestPoint(values);
	lua_pushnumber(pLua_, values[0]);
	lua_pushnumber(pLua_, values[1]);
	return 2;
}

static int motionRange(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	std::string name = luaL_checkstring(pLua_, 2);
	const auto& shadowAnimations = shadowAnimationTable();
	auto found = shadowAnimations.find(name);
	if(found == shadowAnimations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.sourceEngine != STH2005_ONE || rEntity.instanceId == UninitializedPoolId) return luaL_error(pLua_, "entity %I has no ONE instance", toHandle(id));
	const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(rEntity.instanceId);
	const STH2005::Motion& rMotion = Graphics::oneArchives[rInstance.archiveId].motions[Graphics::oneArchives[rInstance.archiveId].useMotion(*found->second.pMotion)];
	lua_pushnumber(pLua_, rMotion.start);
	lua_pushnumber(pLua_, rMotion.end != 0 ? rMotion.end : rMotion.lastFrame);
	return 2;
}

/* blink(e, name): draws that animation in place of the one animate set while it is on, which keeps its own clock; blink(e, false) or nil ends it. */
static int blink(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	if(!lua_isstring(pLua_, 2))
	{
		blinkAnimation(id, nullptr, nullptr);
		return 0;
	}
	const auto& animations = animationTable();
	std::string name = lua_tostring(pLua_, 2);
	auto found = animations.find(name);
	if(found == animations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
	blinkAnimation(id, found->second.pMotion, found->second.pMesh);
	return 0;
}

/* Starts (on) or lets go of (off, it then fades out) the entity's dash ribbon (Graphics::DashTrail); on again while it has one attached does nothing. */
static int setDashTrail(lua_State* pLua_)
{
	Graphics::dashTrail.setActive(checkEntity(pLua_, 1), lua_toboolean(pLua_, 2) != 0);
	return 0;
}

/* Starts (on) or ends (off) one of the entity's effect balls (Graphics::EffectBalls): kind is "jumpA", "dashB" or "fallB"; on again while it has that one does nothing. */
static int setEffectBall(lua_State* pLua_)
{
	std::string kind = luaL_checkstring(pLua_, 2);
	Graphics::EffectBalls::Kind ball;
	if(kind == "jumpA") ball = Graphics::EffectBalls::JumpBallA;
	else if(kind == "dashB") ball = Graphics::EffectBalls::DashBallB;
	else if(kind == "fallB") ball = Graphics::EffectBalls::FallBallB;
	else return luaL_error(pLua_, "sf.setEffectBall: unknown kind '%s' (jumpA, dashB or fallB).", kind.c_str());
	Graphics::effectBalls.setActive(checkEntity(pLua_, 1), ball, lua_toboolean(pLua_, 3) != 0);
	return 0;
}

/*
Makes one sphere of SADX Sonic's homing burst ("burst", fixed where it is made), aura ("aura", follows the entity one tick behind; made every tick while rolling fast or homing) or spin dash charge ("charge", a flattened tilted ellipsoid
at the entity's current position; made every tick while charging) at the entity's position plus (dx, dy, dz) (Graphics::DashSpheres).
heading (radians, default 0) turns the aura's gradient along the character's heading; the charge is drawn in the frame of heading and the up direction (nx, ny, nz) (default 0, 1, 0).
*/
static int spawnDashSphere(lua_State* pLua_)
{
	std::string kind = luaL_checkstring(pLua_, 1);
	Graphics::DashSpheres::Kind sphere;
	if(kind == "burst") sphere = Graphics::DashSpheres::Burst;
	else if(kind == "aura") sphere = Graphics::DashSpheres::Aura;
	else if(kind == "charge") sphere = Graphics::DashSpheres::Charge;
	else return luaL_error(pLua_, "sf.spawnDashSphere: unknown kind '%s' (burst, aura or charge).", kind.c_str());
	float heading = static_cast<float>(luaL_optnumber(pLua_, 6, 0.0));
	glm::vec3 up(static_cast<float>(luaL_optnumber(pLua_, 7, 0.0)), static_cast<float>(luaL_optnumber(pLua_, 8, 1.0)), static_cast<float>(luaL_optnumber(pLua_, 9, 0.0)));
	Graphics::dashSpheres.spawn(checkEntity(pLua_, 2), sphere, checkVector(pLua_, 3), heading, up);
	return 0;
}

/*
Makes one trail segment of SADX Sonic's roll and homing flight (Graphics::SonicTrail): it starts at the entity's position plus (dx, dy, dz) now and ends at the same offset position on the next gameplay tick, lives 20 ticks.
*/
static int spawnDashTrail(lua_State* pLua_)
{
	Graphics::sonicTrail.spawn(checkEntity(pLua_, 1), checkVector(pLua_, 2));
	return 0;
}

/* Makes a player entity from the character's prefab (spawnPrefab: model, idle motions, hand-made collision shape and physics of weight 1) and returns it; the script attaches it (attachPlayer). */
static int spawnPlayer(lua_State* pLua_)
{
	std::string character = luaL_checkstring(pLua_, 1);
	SpawnParams params;
	params.position = checkVector(pLua_, 2);
	PoolId id;
	if(character == "sonic")
	{
		params.name = "SASonic";
		params.what = "(player)";
		id = spawnPrefab(findPrefab(SA_NINJA, "Sonic"), shaderByName(gameShaderName(SA_NINJA)), params);
	}
	else if(character == "shadow")
	{
		params.name = "Shadow";
		params.what = "(player)";
		id = spawnPrefab(findPrefab(STH2005_ONE, "Shadow"), shaderByName(gameShaderName(STH2005_ONE)), params);
	}
	else return luaL_error(pLua_, "unknown character '%s'", character.c_str());
	if(id == UninitializedPoolId) return luaL_error(pLua_, "could not spawn '%s'", character.c_str());
	return pushSpawned(pLua_, id);
}

void registerGameAPI(lua_State* pLua_)
{
	const luaL_Reg functions[] = {
		{"animate", guarded<animate>}, {"blink", guarded<blink>}, {"pose", guarded<pose>}, {"setAnimationScaleMode", guarded<setAnimationScaleMode>}, {"setAnimationEulerOrder", guarded<setAnimationEulerOrder>}, {"motionRange", guarded<motionRange>}, {"bone", guarded<bone>}, {"lowestPoint", guarded<lowestPoint>},
		{"setDashTrail", guarded<setDashTrail>}, {"setEffectBall", guarded<setEffectBall>}, {"spawnDashSphere", guarded<spawnDashSphere>}, {"spawnDashTrail", guarded<spawnDashTrail>}, {"spawnPlayer", guarded<spawnPlayer>},
		{nullptr, nullptr}
	};
	luaL_setfuncs(pLua_, functions, 0);
}
}
