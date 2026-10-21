/*
The members of Level that are the game's (included inside the struct by Level.h; defined in Game/GameLevel.cpp). setSAGPUInstance (an SADX land table) and setONEGPUInstance (a Shadow stage) give the level its instance, setSACollision / setONECollision its collision, and
setONEEntities / setSAEntities the objects the stage places.
*/
void setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_, const std::vector<SATextureReplacement>& rReplacedTextures_ = {});
void setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_);
void setONEEntities(PoolId shaderId_, const STH2005::LevelKey& rLevel_);
void setSAEntities(PoolId shaderId_, const SALandTableKey& rLandTable_);
void setSACollision(const SALandTableKey& rLandTable_);
void setONECollision(const STH2005::LevelKey& rLevel_);
