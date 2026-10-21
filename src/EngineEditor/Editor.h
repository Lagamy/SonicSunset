#pragma once

#ifdef EDITOR
#include "EditorCamera.h"
#include "Gizmo.h"
#include <GLFW/glfw3.h>
#include "DebugUI.h"
#include "CollisionView.h"
#include "Text.h"
#include "LevelLoader.h"
#include "Level.h"
#include "HubWorld.h"
#include "EntityListPanel.h"
#include "InspectorPanel.h"
#include "PrefabPanel.h"
#include "CollisionShape.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include "ExternalField.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

/*
Everything of the editor in one place: the debug UI and its panels, the collision view, the entity labels, the transform gizmo of the selected entity, the keys that drive them and a camera of its own. Only built with EDITOR (CMake); the gameplay
never reads it except through editorActive().
The editor is active (`active`; the toolbar's Play / Stop switch, setPlaying) from the start. While it is, the editor camera is what the screen shows (Renderer::runAll) and what the fly keys (W A S D, Space, Ctrl, Shift), the mouse buttons (right turn, left pan) and the wheel (zoom) move, with the cursor visible (EditorCamera.h); the gameplay's cameras
(Globals::Game::cameras, mainCamera) are not drawn on the screen, do not take the mouse and keep following their target, so nothing done in the editor moves them. The camera is not an entity and not in the camera pool: it has no
entry in the entity list and the scripts cannot reach it. Gameplay culling (GameplaySystem::tick) keeps using the gameplay main camera.
Order: init after the renderer, the scripts' main file and the main camera; processInputs and draw once per frame; destroy before the renderer.
*/
struct Editor
{
	/* True while in the editor (the start state), false while playing (the Play button, setPlaying). */
	bool active = true;

	/* The editor's HubWorld view (the HubWorld tab, docs/TODO.md level HubWorld): every level is drawn and picked. False = the Level view, where only the active level is (levelShown). Has no effect while playing. */
	bool hubWorldView = false;

	/* The HubWorld tab's Entities subtab is shown (EntityListPanel::draw sets it each frame): every level is drawn like in the HubWorld view, but the gizmo, the Inspector and picking work on entities, and the entity list holds only the entities the HubWorld owns (Entity::hubWorldOwned). Entities made now belong to the HubWorld. */
	bool hubWorldEntities = false;

	/*
	The HubWorld view's state: the name of the level whose place is being edited (hubWorldSelected; empty = the active level) and whether HubWorld.json has a change that is not written yet (hubWorldDirty; set while a drag or the inspector widget runs). When it is let go (processInputs) the change counts as made (hubWorldUnsaved), and HubWorld.json is written only by Save (saveAllLevels, Ctrl+S).
	levelPlace gives a loaded level's place (the HubWorld's entry, else the level's own); false if no level of the name is loaded. setLevelPlace gives the level the place (Level::setHubWorldTransform) and keeps it in the HubWorld (HubWorld::set).
	*/
	std::string hubWorldSelected;
	bool hubWorldDirty = false;
	bool hubWorldUnsaved = false;

	/*
	The levels selected besides hubWorldSelected (a multi-selection: Shift or Ctrl click in the HubWorld tab's list, Shift click in the scene) and the helpers on them. hubWorldSelection lists the selected names, hubWorldSelected first (empty = the active level's name), loaded levels only. selectHubWorldLevel selects one
	level, or with additive_ adds or removes it from the selection (a newly added one becomes hubWorldSelected). The HubWorld view's gizmo moves and turns them all as a group; Ctrl+Z undoes such a change.
	*/
	std::vector<std::string> hubWorldExtraSelected;

	std::vector<std::string> hubWorldSelection() const;
	bool hubWorldIsSelected(const std::string& name_) const;
	void selectHubWorldLevel(const std::string& name_, bool additive_);
	bool levelPlace(const std::string& name_, HubWorldLevel& rPlace_) const;
	void setLevelPlace(const HubWorldLevel& rPlace_);

	/* The HubWorld tab's "Choose as active": loads the HubWorld with the level active (as Load HubWorld does, unsaved edits gone) and goes back to the Level tab. */
	void chooseActive(const std::string& name_);

	/* The HubWorld tab's "Add level to HubWorld": the level gets an entry (at the origin) and, if it is not loaded, is loaded in its place beside the others; the HubWorld is written. Says in fileMessage how it went. */
	void addLevelToHubWorld(const std::string& name_);

	/*
	The HubWorld tab's "Remove from HubWorld": the level's entry goes (HubWorld.json is written). A loaded level that is not the active one is unloaded; the active one stays and goes back to the origin, as it would load without an entry. The Level's file and edits are not touched.
	*/
	void removeLevelFromHubWorld(const std::string& name_);

	/*
	While playing, a level was reset in place (Loading::resetLevel): puts back what Play kept of it (keptLevels, as Stop does), the entities' state and the edits made in the editor before Play. Nothing if Play kept no level of that name.
	*/
	void restoreKeptLevel(PoolId levelId_);
	/*
	The in-place reset (Loading::resetEntitiesInPlace) of a level Play kept: gives the entities it made again (rRemadeIndices_, their place in the level) their kept state, and makes the entities the editor added again (all of them were removed). The entities it left alone have the state Play
	kept (Play took their baseline from it). The caller resolves the external references after. Nothing if Play kept no level of that name.
	*/
	void restoreKeptEntities(PoolId levelId_, const std::vector<uint32_t>& rRemadeIndices_);
	/* Whether Play kept this level's edits (restoreKeptLevel would give them back). */
	bool hasKeptLevel(const std::string& rName_) const;

	/* While playing: the debug menu is open (Tab), with the cursor free for it. Always false in the editor. */
	bool debugMenuOpen = false;

	/* True while the cursor is free (visible, for the debug UI) and the mouse movement is not read: whenever the editor is active except during a button hold (EditorCamera::dragging). */
	bool cursorFree = false;

	/* False until init, so the window callbacks (which can run earlier) leave the camera alone. */
	bool initialized = false;

	EditorCamera camera;

	/* The distance to the model the camera looks at and the time until it is measured again (processInputs). */
	float speedProbeDistance = 0.0f;
	float speedProbeTimer = 0.0f;
	Graphics::DebugUI debugUI;
	Graphics::CollisionView collisionView;
	Graphics::Text text;
	Graphics::EntityListPanel entityList;
	Graphics::InspectorPanel inspector;
	Graphics::PrefabPanel prefabPanel;
	Graphics::Gizmo gizmo;

	/* The state of the keys the last time processInputs saw them, so that one press counts once. */
	bool tabPressed = false;
	bool escapePressed = false;
	bool cPressed = false;
	bool homePressed = false;
	bool vPressed = false;
	bool zPressed = false;
	bool deletePressed = false;
	bool yPressed = false;
	bool sPressed = false;
	bool leftPressed = false;
	bool rightPressed = false;
	bool upPressed = false;
	bool downPressed = false;
	bool probePressed = false;
	bool gizmoKeyPressed[3] = {false, false, false};

	/* Click to select (processInputs): a left press the UI and the gizmo did not take is pending until the button is released (a camera hold cancels it). clickX / clickY: where the press was. */
	bool clickLeftWas = false;
	bool clickPending = false;
	double clickX = 0.0;
	double clickY = 0.0;

	/* The first loaded level, UninitializedPoolId if none: the one the editor edits and saves (one level is loaded at a time). */
	PoolId currentLevel() const;

	/* The File menu's work (DebugUI): writes the current level's edits into the level file of the name (Loading::saveLevelEdits) and says in fileMessage how it went; or puts the level back as its source has it (the game's data plus the level's saved Source Game Level; Level::restoreSource, unsaved until Save). */
	void saveLevel(const std::string& fileName_);

	/* Save and Ctrl+S: writes every loaded level's edits into its own level file (saveLevelEdits; a level whose file would not change is not rewritten), one failing level does not stop the others; fileMessage says how many were saved. Save as writes only the current level (saveLevel). */
	void saveAllLevels();
	void restoreSourceValues();

	/* File > Save Source Game Level: makes the level as it is now the level's source (Loading::saveSourceLevel: Restore Source Game Level returns to it from now on), then loads the level again from its files so the editor holds exactly what a load gives (the selection is cleared, the players go to the start); says in fileMessage how it went. */
	void saveSourceLevel();

	/* Replaces the loaded level with Levels/<name>/<name>.json (the scripts' resetGame with a name: the level, the players at its start); what was edited and not saved in the old level is gone. pMode_ is resetGame's: "level" loads it alone (File > Load level), "hub" loads the HubWorld with it active, nullptr keeps the kind of the last load. */
	void loadLevel(const std::string& levelName_, const char* pMode_ = "level");

	/* File > Load HubWorld: loads every level of HubWorld.json at its place (the first entry is the active level); unsaved edits are gone. Says in fileMessage if HubWorld.json has no levels. */
	void loadHubWorld();

	std::string fileMessage;

	/* The File menu's New level (DebugUI): writes the level file (Loading::createLevelFile) and, if that worked, loads it as loadLevel does; says in fileMessage how it went. */
	void newLevel(const Loading::NewLevelFile& level_);

	/* The File menu's glTF level items (DebugUI; Loading::swapGLTFLevelModel / addGLTFLevelCollision / swapGLTFLevelCollision on the current level): each says in fileMessage how it went. Not run. */
	void swapLevelModel(const std::string& gltfPath_);
	void addLevelCollision(const std::string& gltfPath_);
	void swapLevelCollision(const std::string& name_, const std::string& gltfPath_);

	/*
	The Import menu (DebugUI; Loading::importExternalTexture / importExternalModel): a PNG or JPEG becomes an external texture, a glTF/.glb an external model, in the project's assets or, with inLevel_, in the open level's. An empty name_ takes the file's name.
	A model's textures are imported as "<model>_tex<i>"; one that exists already in the scope is kept as it is. Each says in fileMessage how it went. Not run.
	*/
	void importTexture(const std::string& path_, const std::string& name_, bool inLevel_);
	void importModel(const std::string& gltfPath_, const std::string& name_, bool inLevel_);

	/* File > Export Library.gltf (DebugUI; Loading::exportLibrary, docs/TODO.md HubWorld.gltf pipeline): writes Library.gltf and its textures into the folder, and says in fileMessage how many models were written, skipped or repeated. Not run. */
	void exportLibrary(const std::string& folder_);

	/*
	What setPlaying keeps of one entity at Play for the way back. A property the editor can edit and Stop must give back is a field here (captured in one place, Editor.cpp keptEntityOf, and given back in one, giveKeptEntity), not a vector of its own.
	*/
	struct KeptEntity
	{
		glm::vec3 position;
		glm::mat4 rotation;
		glm::vec3 scale;
		std::vector<PoolId> behaviors;
		std::vector<Physics::CollisionShape> shapes;
		TextureChoice texture;
		MeshChoice mesh;
		/* The external field values (Entity::externals); an entity reference is kept as entityIndex (Level::editIndexOf). */
		std::map<std::string, std::map<std::string, ExternalValue>> externals;
		std::map<std::string, std::map<std::string, std::string>> externalsKept;
	};

	/* A level's own entity, by its index in Level::entityIds. */
	struct KeptOwn
	{
		uint32_t index;
		KeptEntity entity;

		/* The entity's id when Play kept it: undo steps that name it are moved to the new one (renameUndoEntity). */
		PoolId oldId;
	};

	/* An entity added to the level, made again from its prefab. */
	struct KeptAdded
	{
		PoolId prefabId;
		KeptEntity entity;
		PoolId oldId;

		/* The entity's place in Level::entityIds when Play kept it (restoreKeptEntities tells the editor's from the build's). */
		uint32_t index;
	};

	/* The prefabs' own edits (behaviours, bound area shapes, model, texture). */
	struct KeptPrefab
	{
		PoolId prefabId;
		std::vector<PoolId> added;
		std::vector<PoolId> removed;
		std::vector<Physics::CollisionShape> addedShapes;
		std::vector<Physics::CollisionShape> removedShapes;
		TextureChoice texture;
		MeshChoice mesh;
	};
	void clearKept();

	/* An entity's position, rotation and scale: what Ctrl+Z gives back. */
	struct Transform
	{
		glm::vec3 position;
		glm::mat4 rotation;
		glm::vec3 scale;
		bool operator==(const Transform&) const = default;
	};
	struct EntityTransform
	{
		PoolId entity;
		Transform transform;
	};

	/* Ctrl+C: what was copied, a prefab entity (prefabId, entity), a camera entity (isCamera, camera) or a plain entity (isPlain, entity). */
	struct CopiedCamera
	{
		glm::vec3 front;
		float yaw;
		float pitch;
		float fov;
		float nearPlane;
		float farPlane;
		float speed;
		float speedUp;
		bool enabled;
		/* The main camera (a paste clears it: a copy never takes over), and the size of its render target (0 = the screen; a copy gets a render target of its own of that size). */
		bool isMain;
		uint32_t targetWidth;
		uint32_t targetHeight;
	};
	struct Clipboard
	{
		bool valid = false;
		bool isCamera = false;
		/* A plain entity (no prefab, no camera: Add empty entity); entity holds its state. */
		bool isPlain = false;
		/* The HubWorld owns the copy's entity (a plain or prefab entity of the HubWorld tab), not the level. */
		bool hubWorld = false;
		std::string name;
		PoolId prefabId = UninitializedPoolId;
		KeptEntity entity;
		CopiedCamera camera;
	};
	/*
	One step of Ctrl+Z / Ctrl+Y: either the entity's transform to give back (created false; before), or an entity the editor made (created true: undoing removes it, and keeps its state in snapshot so that redoing makes it again, with a new PoolId).
	Two more kinds, both with created false: a group (group not empty: the transforms several entities had before one change, entity / before are the first of them) and a HubWorld step (places not empty: the places the levels had in the HubWorld view, given back by name).
	*/
	struct UndoAction
	{
		bool created;
		PoolId entity;
		Transform before;
		Clipboard snapshot;
		std::vector<EntityTransform> group;
		std::vector<HubWorldLevel> places;
	};
	std::vector<UndoAction> undoStack;
	std::vector<UndoAction> redoStack;

	/* The selected entities (primary first) and their transforms as last seen (trackTransform), and in the HubWorld view the selected levels' places: a different value once nothing is being dragged is a finished change. */
	std::vector<EntityTransform> tracked;
	bool trackedBusy = false;
	std::vector<HubWorldLevel> trackedPlaces;
	bool trackedPlacesBusy = false;

	Clipboard clipboard;

	/*
	What setPlaying keeps of one level at Play, by the level's name (its pool slot is gone and made again at Stop): the level's own entities, the added ones, the prefabs' edits for that level, and its camera entities (snapshotOf; a camera is not in added, it has no prefab).
	Every level alive at Play is kept: play can leave a level (it is unloaded) and Stop must give each back, not only the one the game ends in.
	*/
	struct KeptLevel
	{
		std::string name;
		std::vector<KeptOwn> own;
		std::vector<KeptAdded> added;
		std::vector<KeptPrefab> prefabs;
		std::vector<Clipboard> cameras;

		/* Level::removed at Play: Stop removes these again. */
		std::vector<uint32_t> removed;

		/* The place in Level::entityIds of each of cameras, at Play. */
		std::vector<uint32_t> cameraIndices;
	};
	std::vector<KeptLevel> keptLevels;

	/* The HubWorld's entities at Play (snapshotOf, hubWorld true) and the ids they had; Stop makes them again (the HubWorld's entities are not in a level). */
	std::vector<Clipboard> keptHubWorld;
	std::vector<PoolId> keptHubWorldIds;

	/* A camera entity at a position that the level owns (it is saved with the level, Loading::saveLevelEdits "cameras"), or the HubWorld (hubWorld_: HubWorld.json "entities"); the Add camera button and paste make cameras with it. */
	PoolId addLevelCamera(const char* name_, const glm::vec3& rPosition_, PoolId levelId_ = UninitializedPoolId, bool hubWorld_ = false);

	/* Puts an entity the editor made (Add camera, paste) on the undo stack and selects it. */
	void recordCreated(PoolId entityId_);

	/* Gives a new entity (addEntity, spawnPrefab) to its owner: the HubWorld (hubWorld_ true: Entity::hubWorldOwned, HubWorld::entityIds; HubWorld.json is unsaved) or the level (levelId_, else the current one: Entity::levelId, Level::entityIds). */
	void adoptEntity(PoolId entityId_, bool hubWorld_, PoolId levelId_ = UninitializedPoolId);

	/* Whether the entity is listed in the HubWorld's Entities subtab (and not in the Level tab): the HubWorld's own, and, while the levels are loaded as a HubWorld, the ones no level owns (the main camera, the players, a script's spawns). With a single level loaded they are in the Level tab. */
	bool inHubWorldList(const Entity& rEntity_) const;

	/* Delete key and the entity list's Delete: removes the selected entities that can go, each one an undo step (Ctrl+Z makes it again from a snapshot). The entities a level makes from the game's data can't (message in fileMessage). */
	void deleteSelected();
	/* Takes an entity out of its owner and the pool (the owner's list, selection, spatial index); the part that Delete and undoing a create share. */
	void removeEntity(PoolId entityId_);

	/* Ctrl+Z undoes the latest step, Ctrl+Y does the latest undone one again; a new step empties the redo ones. Both stacks are dropped when the level is loaded again or restored; Play and Stop keep them (the entities made again at Stop are renamed in the steps, giveKeptLevel). */
	void undo();
	void redo();
	void pushUndo(const UndoAction& rAction_);
	void applyAction(const UndoAction& rAction_, std::vector<UndoAction>& rOpposite_);
	Clipboard snapshotOf(PoolId entityId_);
	PoolId spawnFrom(const Clipboard& rCopy_, const glm::vec3& rPosition_, PoolId levelId_ = UninitializedPoolId);

	/* Gives a level what keptLevels holds for it (Stop): the prefabs' edits for that level, each entity's exact state, the added entities and cameras made again. */
	void giveKeptLevel(PoolId levelId_, const KeptLevel& rKept_);
	void clearUndo();

	/* Makes the undo and redo steps that name entity old_ name new_ (the entity was made again by Stop or a level reset). */
	void renameUndoEntity(PoolId old_, PoolId new_);

	/* Looks at the selected entities (the selected levels in the HubWorld view) once a frame (processInputs, in the editor): when a transform (place) differs from the last one seen and no gizmo drag or ImGui widget is being held, pushes the old ones (a whole drag is one step; several changed entities or levels are one step). */
	void trackTransform();
	void trackPlaces();

	/* Ctrl+C copies the selected entity; Ctrl+V makes a copy where the cursor points (the horizontal plane through the copied entity, else where it was), selected and undoable. A camera entity copies the camera's own settings (not its render target). Says in fileMessage when it can't. */
	void copySelected();
	void pasteAtCursor();

	/*
	editor_settings.json (next to Assets/, in the working directory): the panel sizes (the user drags their edges) and the editor camera's settings (EntityListPanel's "Editor camera" tab). init reads it (a missing or broken file
	leaves the defaults); destroy writes it only when its content differs from what init read, so a run with no change leaves the file alone.
	*/
	void loadSettings();
	void saveSettings();
	std::string savedSettings;

	void init(GLFWwindow* pWindow_);

	/* Puts the editor camera where the gameplay main camera is and looks where it looks (not its planes or fov). Done when the editor becomes active; main.cpp also does it once the level script has placed the main camera. */
	void matchGameplayCamera();

	/* Enters play (the gameplay camera is the view, the cursor is captured, the game ticks) or goes back to the editor (the editor camera starts at the gameplay camera). Going back resets the game (Scripting::resetGame: the level is loaded again, the players go to its start); the level's entities keep the positions and additions the editor had at Play, saved or not. */
	void setPlaying(bool playing_);

	/* Reads the editor's keys and mouse buttons: fly, the gizmo (1 / 2 / 3 pick its mode, its handle drags come before the camera's button hold), button holds, Tab (the debug menu, while playing), C, V and the arrows. With Ctrl held (either one): Z undoes, Y redoes, C copies, V pastes and S saves the level (Editor::saveLevel); C and V toggle nothing and the fly keys stop meanwhile. Not while an ImGui text field has the keyboard. */
	void processInputs();

	/* The editor camera's reaction to a mouse movement (Window callback, Utils.h mouseCallback): turns or pans it during a right button hold. */
	void onMouse(float xOffset_, float yOffset_);

	/* The editor camera's reaction to the wheel (Window callback, Utils.h scrollCallback): zoom, unless the debug UI is using the wheel. */
	void onScroll(float yOffset_);

	/* Draws the debug UI over the screen (after Renderer::runAll), if active. */
	void draw();

	void destroy();
};
inline Editor editor;

inline bool editorActive()
{
	return editor.active;
}
#else
/* The editor does not exist in this build: it is never active. */
inline bool editorActive()
{
	return false;
}
#endif
