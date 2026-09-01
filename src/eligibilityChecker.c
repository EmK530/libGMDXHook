#include "eligibilityChecker.h"

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "external/xxhash/xxhash.h"

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
    eligibilityObj = cJSON_GetObjectItemCaseSensitive(root, "4bppEligibleTextureHashes");
    if(!cJSON_IsObject(eligibilityObj)) {
        printf("[D3DHook::EC] 4bppEligibleTextureHashes in '" targetFile "' is missing or wrong type, BC1 disabled.\n");
        eligibilityObj = NULL;
        return;
    }
}

bool EC_IsBC1Eligible(const uint8_t* pSrcData, UINT pSrcDepth) {
    if(eligibilityObj == NULL)
        return false;
    XXH64_hash_t hash = XXH3_64bits(pSrcData, pSrcDepth);
    char hashHex[17];
    snprintf(hashHex, sizeof(hashHex), "%016" PRIX64, hash);
    bool eligible = false;
    cJSON* check = cJSON_GetObjectItemCaseSensitive(eligibilityObj, hashHex);
    if(check != NULL && cJSON_IsBool(check) && cJSON_IsTrue(check))
        eligible = true;
    printf("[D3DHook::EC] Incoming texture data hash: %s (eligible = %s)\n", hashHex, eligible ? "true" : "false");
    return eligible;
}