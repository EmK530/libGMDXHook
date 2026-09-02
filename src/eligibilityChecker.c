#include "eligibilityChecker.h"
#include "rootConfig.h"

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "external/xxhash/xxhash.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb/stb_image_write.h"

#include "external/cJSON/cJSON.h"

#include <stdio.h>
#include <inttypes.h>

#define targetFile "vsD3DSettings.json"

cJSON* root = NULL;
cJSON* eligibilityObj = NULL;
void EC_Init() {
    char* jsonBufTemp = calloc(1, 65536);
    if(!jsonBufTemp) {
        printf("[D3DHook::EC] Failed to allocate memory for JSON file, BC1 disabled.\n");
        return;
    }
    HANDLE hnd = CreateFileA(targetFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hnd == INVALID_HANDLE_VALUE)
    {
        printf("[D3DHook::EC] Failed to open '" targetFile "', BC1 disabled.\n");
        free(jsonBufTemp);
        return;
    }
    DWORD bytesRead = 0;
    if(!ReadFile(hnd, jsonBufTemp, 65536, &bytesRead, NULL) || bytesRead == 65536) {
        printf("[D3DHook::EC] Failed to read '" targetFile "' or data is too large, BC1 disabled.\n");
        CloseHandle(hnd);
        free(jsonBufTemp);
        return;
    }
    CloseHandle(hnd);
    root = cJSON_Parse(jsonBufTemp);
    free(jsonBufTemp);
    if(root == NULL) {
        printf("[D3DHook::EC] Failed to parse '" targetFile "', BC1 disabled.\n");
        return;
    }

    cJSON* tempObj = cJSON_GetObjectItemCaseSensitive(root, "enable4bppCompression");
    enableBC1Textures = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;
    tempObj = cJSON_GetObjectItemCaseSensitive(root, "enableTextureCache");
    enableTextureCache = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;
    tempObj = cJSON_GetObjectItemCaseSensitive(root, "deleteUnusedTextureCachesOnExit");
    deleteUnusedTextureCachesOnExit = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;

    tempObj = cJSON_GetObjectItemCaseSensitive(root, "_experiment_supportNonAtlases");
    experimentSupportNonAtlases = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;

    tempObj = cJSON_GetObjectItemCaseSensitive(root, "_debug_force4bppCompression");
    debugForceBC1Textures = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;
    cJSON* dumpObj = cJSON_GetObjectItemCaseSensitive(root, "_debug_textureDumping");
    if(cJSON_IsObject(dumpObj)) {
        tempObj = cJSON_GetObjectItemCaseSensitive(dumpObj, "_debug_dumpUnrecognizedTextureHashes");
        debugDumpUnrecognizedTextures = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;
        tempObj = cJSON_GetObjectItemCaseSensitive(dumpObj, "_debug_dumpAllTextureHashes");
        debugDumpAllTextures = cJSON_IsBool(tempObj) ? cJSON_IsTrue(tempObj) : false;
    }

    eligibilityObj = cJSON_GetObjectItemCaseSensitive(root, "4bppEligibleTextureHashes");
    if(!cJSON_IsObject(eligibilityObj)) {
        printf("[D3DHook::EC] 4bppEligibleTextureHashes in '" targetFile "' is missing or wrong type, BC1 disabled.\n");
        eligibilityObj = NULL;
        return;
    }
}

bool EC_IsBC1Eligible(const uint8_t* pSrcData, UINT width, UINT height, UINT pSrcDepth, uint64_t* outHash) {
    XXH64_hash_t hash = XXH3_64bits(pSrcData, pSrcDepth);
    *outHash = hash;
    if(!enableBC1Textures || eligibilityObj == NULL) {
        printf("[D3DHook::EC] Incoming texture data hash: %016" PRIX64 " (BC1 is disabled)\n", hash);
        return false;
    }
    if(debugForceBC1Textures) {
        printf("[D3DHook::EC] Incoming texture data hash: %016" PRIX64 " (BC1 is forced on)\n", hash);
        return true;
    }
    char hashHex[17];
    snprintf(hashHex, sizeof(hashHex), "%016" PRIX64, hash);
    bool eligible = false;
    cJSON* check = cJSON_GetObjectItemCaseSensitive(eligibilityObj, hashHex);
    if(check != NULL && cJSON_IsBool(check) && cJSON_IsTrue(check))
        eligible = true;
    printf("[D3DHook::EC] Incoming texture data hash: %s (eligible = %s)\n", hashHex, eligible ? "true" : "false");
    if(debugDumpAllTextures || (check == NULL && debugDumpUnrecognizedTextures)) {
        UINT RGBA8RowPitch  = width * 4;
        UINT RGBA8DepthPitch = RGBA8RowPitch * height;
        printf("[D3DHook::EC] Dumping texture data to PNG...\n");
        char searchPath[MAX_PATH];
        snprintf(searchPath, PATH_MAX, "vsD3DHook_textureDumps/%s.png", hashHex);
        if (GetFileAttributesA(searchPath) == INVALID_FILE_ATTRIBUTES)
        {
            CreateDirectoryA("vsD3DHook_textureDumps", NULL);
            stbi_write_png_compression_level = 0;
            stbi_write_png(searchPath, width, height, 4, pSrcData, width * 4);
        }
    }
    return eligible;
}