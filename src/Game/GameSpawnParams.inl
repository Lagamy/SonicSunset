/* The fields of SpawnParams that are the game's (included inside the struct by Prefab.h). */
const SAModelKey* pModel = nullptr;
const std::vector<SACollisionShapesKey>* pCollisions = nullptr;
const PrefabPlacement* pPlacement = nullptr;
uint32_t modelIndex = 0;
const std::vector<STH2005::CollisionKey>* pONECollisions = nullptr;
