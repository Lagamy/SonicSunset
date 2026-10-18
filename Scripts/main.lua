-- Entry script (main.cpp loads it). The engine calls attachPlayer(entity) once for each playable entity and the prefab behaviours registered with sf.prefabBehavior for every object a level makes (sadx/objects.lua). One player is active at a time:
-- only the active one reads the keys (state.active, read by both player scripts), the other stands where it is under the same physics. The first one attached is active; J switches, moving only the camera (sf.followCamera)
-- to the new one. R puts every player back at the position it had when it was attached, with no velocity.
sf.include("sadx/main.lua")
sf.include("shadow/main.lua")

local ATTACH = {SASonic = attachSonicPlayer, Shadow = attachShadowPlayer}
local INIT_STATE = {SASonic = initSonicState, Shadow = initShadowState}

local players = {}
local active = 0
local starts = {}
local jHeld = false
local rHeld = false

local function activate(index)
	active = index
	for i, entity in ipairs(players) do sf.state(entity).active = i == index end
	sf.followCamera(players[index])
end

sf.behavior("PlayerSwitch", function(entity)
	local controlled = not sf.debugMode()
	local jPressed = controlled and sf.key("J")
	local switch = jPressed and not jHeld
	jHeld = jPressed
	local rPressed = controlled and sf.key("R")
	local respawn = rPressed and not rHeld
	rHeld = rPressed
	if switch and #players >= 2 then activate(active % #players + 1) end
	if respawn then
		for i, player in ipairs(players) do
			local start = starts[i]
			sf.setPosition(player, start[1], start[2], start[3])
			sf.setVelocity(player, 0, 0, 0)
		end
		if ShadowReplay.enabled then
			for i, player in ipairs(players) do
				if sf.name(player) == "Shadow" then
					activate(i)
					ShadowReplay.start(player)
				end
			end
		end
		if SadxReplay.enabled then
			for i, player in ipairs(players) do
				if sf.name(player) == "SASonic" then
					activate(i)
					SadxReplay.start(player)
				end
			end
		end
	end
end)

function attachPlayer(entity)
	local attach = ATTACH[sf.name(entity)]
	if attach == nil then error("no player script for the entity '" .. sf.name(entity) .. "'") end
	attach(entity)
	players[#players + 1] = entity
	starts[#players] = {sf.position(entity)}
	if #players == 1 then
		sf.give(entity, "PlayerSwitch")
		activate(1)
	else
		sf.state(entity).active = false
	end
end

-- Gives every player's state table (sf.state) its starting contents again, the active flag kept as it is (game.lua calls it when the engine resets the game).
function resetPlayers()
	for i, player in ipairs(players) do
		local state = sf.state(player)
		for key in pairs(state) do state[key] = nil end
		INIT_STATE[sf.name(player)](state)
		state.active = i == active
	end
end

-- Records the players' current positions as the ones R returns them to (game.lua calls it after moving them to a new level's start).
function recordPlayerStarts()
	for i, player in ipairs(players) do starts[i] = {sf.position(player)} end
end

-- Triggers that change the level. The collision behaviours LoadLevel and LoadHubWorld (tags of the same names) act on a Body that touches them; the level to load is the string `level` of the touching shape's own values
-- (the third argument of the interaction function), set per shape in the BoundArea tab of the editor's Inspector (a file of Levels/; for LoadHubWorld the level that is active, empty = the first entry of HubWorld.json).
-- LoadLevelFields only declares that field: the collision behaviours name it with fields = "LoadLevelFields". game.lua does the change (requestLevelLoad).
sf.behavior("LoadLevelFields", function(entity) end, {level = {type = "level", default = "", external = true}})

local tagBody = sf.tag("Body")
local tagLoadLevel = sf.tag("LoadLevel")
local tagLoadHubWorld = sf.tag("LoadHubWorld")

local function triggerTouched(trigger, player, shape, hub)
	if not sf.exists(trigger) or not sf.hasBody(player) then return end
	local name = shape and shape.level
	if name == nil or name == "" then name = nil end
	if not hub and name == nil then return end
	requestLevelLoad(name, hub)
end

sf.collisionBehavior("LoadLevel", {tags = {tagLoadLevel}, fields = "LoadLevelFields", interactions = {[tagBody] = function(trigger, player, shape) triggerTouched(trigger, player, shape, false) end}})
sf.collisionBehavior("LoadHubWorld", {tags = {tagLoadHubWorld}, fields = "LoadLevelFields", interactions = {[tagBody] = function(trigger, player, shape) triggerTouched(trigger, player, shape, true) end}})
