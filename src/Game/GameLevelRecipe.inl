/* The fields of Loading::Recipe that are the game's (included inside the struct by LevelRecipe.h): the keys of a "sadx" or a "sth2005" level; the other one is null. */
std::unique_ptr<SALandTableKey> saLandTable;
std::unique_ptr<SATextureSetKey> saTextures;
std::vector<SATextureReplacement> saReplacedTextures;
std::unique_ptr<STH2005::LevelKey> oneLevel;
