/* The fields of Prefab that are the game's (included inside the struct by Prefab.h). saKey / oneKey point into the generated entity tables, which live for the whole run. */
const SAEntityKey* saKey = nullptr;
const STH2005::EntityKey* oneKey = nullptr;
PlacementLayout layout = PlacementLayout::Single;
std::vector<std::vector<uint32_t>> variants;
bool loopMotion = false;
std::vector<SARotationStep> rotation;
uint8_t variantField = 0;
