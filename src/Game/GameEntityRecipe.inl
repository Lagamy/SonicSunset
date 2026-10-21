/* The fields of EntityRecipe that are the game's (included inside the struct by Level.h): what a Placed entity needs besides SpawnParams, the copies of what its params pointed at: the SET record and a Shadow record's collision keys. */
std::shared_ptr<const Ninja::SAPlacement> placement;
std::vector<STH2005::CollisionKey> oneCollisions;
bool hasOneCollisions = false;
