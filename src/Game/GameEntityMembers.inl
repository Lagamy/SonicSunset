/*
The members of Entity that are the game's, included inside the struct by Entity.h (the seam between the engine and SonicFantasy; every game supplies its own). Defined in Game/GameEntity.cpp.
An entity is created first and gets its instance afterwards, with setSAGPUInstance or setONEGPUInstance; only one of the two can be called, once: an entity has a single instance.
The instance is created from resource keys (ResourceKeys.h): whatever the keys name that is not loaded yet is loaded then, and it stays loaded while any instance uses it. The textures are optional (the overload without them is untextured).
A ONE instance can switch to another mesh or .TXD entry of its archive later with changeActiveMesh / changeActiveTexture, and replace one part of its mesh with switchMeshPart.
An SA instance plays a motion with playSAMotion (loops), a ONE instance with playONEMotion (loops, or plays once and holds the last frame with loop_ = false); update advances them.
setSACollisionShapes gives the entity the shapes of an SADX collision table (Physics::CollisionShape); setONECollisionShapes does the same for a Shadow .CCL entry (readCCL, whose mapping to shapes is a hypothesis); Shadow himself has no .CCL.
animationIntent is the motion gameplay wants (changeAnimation); update plays it when it differs from appliedAnimation, the last one it played.
*/
AnimationIntent animationIntent;
AnimationIntent appliedAnimation;
/*
A motion drawn in place of animationIntent while it is set (sf.blink; its mesh is switched with it), like the game's ball blink: the intent's motion keeps its own clock meanwhile
(baseFrame, in frames of baseFrameCount) and goes on from there when the blink ends, instead of restarting. appliedBase is the intent last seen, so a new one starts at frame 0.
*/
AnimationIntent blinkIntent;
AnimationIntent appliedBase;
float baseFrame = 0.0f;
uint32_t baseFrameCount = 0;
/* Frames a second the entity's SA motion plays at, set by gameplay (sf.animate); 0 = the default, negative = backwards (SA motions). Not part of the intent: it can change every tick without restarting the motion. */
float animationRate = 0.0f;
/* The blend rate of the SA motion gameplay asked for (sf.animate): factor a second, 0 = none. Read when a motion change is applied (Entity::update); the instance then blends from the pose it showed to the new motion's first frame (SAInstance::beginBlend). */
float animationBlendRate = 0.0f;

void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey& rTextures_);
void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_);
/* The SA instance's icon quad (SAKey iconTextures): the node whose submesh has the texture id ownTexture_ is drawn facing the camera with the TextureSet's texture id texture_. Throws if there is no such node or texture. */
void setSAIcon(uint32_t ownTexture_, uint32_t texture_);
void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_);
void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_);
void changeActiveMesh(const STH2005::ModelKey& rKey_);
void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
void changeActiveTexture(const STH2005::TextureKey& rKey_);
void changeSAMesh(const SAModelKey& rKey_);
void playSAMotion(const SAMotionKey& rKey_);
void playONEMotion(const STH2005::MotionKey& rKey_, bool loop_ = true);
/* A pose of motions mixed by weight at given frames, in place of the main skeleton's playing motions (ONEInstance::setPose); an empty list ends it. ONE instances only. */
void setONEPose(const std::vector<Graphics::PoseRequest>& rLayers_);
void setSACollisionShapes(const SACollisionShapesKey& rShapes_);
void setSACollisionShapes(const std::vector<SACollisionShapesKey>& rShapes_);
void setONECollisionShapes(const STH2005::CollisionKey& rShapes_);
void setONECollisionShapes(const std::vector<STH2005::CollisionKey>& rShapes_);
