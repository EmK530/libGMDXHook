#include "rootConfig.h"

// Constant variables, these get modified by the JSON reader in eligibilityChecker.c

bool enableBC1Textures = false;
bool enableTextureCache = true;
bool deleteUnusedTextureCachesOnExit = false;

bool experimentSupportNonAtlases = false;
int minWidthRequired = 256;
int minHeightRequired = 256;

bool debugForceBC1Textures = false;
bool debugDumpUnrecognizedTextures = false;
bool debugDumpAllTextures = false;