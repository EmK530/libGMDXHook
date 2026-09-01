#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <inttypes.h>

#include "bc7_wrapper.h"
#include "rootConfig.h"
#include "textureCompressor.h"
#include "eligibilityChecker.h"

#define STB_DXT_IMPLEMENTATION
#include "external/stb/stb_dxt.h"

#define XXH_STATIC_LINKING_ONLY
#include "external/xxhash/xxhash.h"

typedef struct {
    const uint8_t* srcRGBA;
    int width;
    int blocksWide;
    int blocksHigh;
    uint8_t* dst;
    volatile LONG nextRow;
} BC1Context;

static DWORD WINAPI BC1Worker(LPVOID param) {
    BC1Context* ctx = (BC1Context*)param;

    while(1) {
        LONG by = InterlockedIncrement(&ctx->nextRow) - 1;
        if (by >= ctx->blocksHigh)
            break;

        for (int bx = 0; bx < ctx->blocksWide; bx++) {
            uint8_t block[64];

            for (int y = 0; y < 4; y++) {
                const uint8_t* srcRow = ctx->srcRGBA + ((by * 4 + y) * ctx->width + bx * 4) * 4;
                memcpy(block + y * 16, srcRow, 16);
            }

            size_t blockIndex = (size_t)by * ctx->blocksWide + bx;
            stb__CompressBC1AlphaBlock(ctx->dst + blockIndex * 8, block, STB_DXT_HIGHQUAL);
        }
    }

    return 0;
}

void compress_bc1(const uint8_t* srcRGBA, uint32_t width, uint32_t height, uint8_t* dst) {
    const int blocksWide = width / 4;
    const int blocksHigh = height / 4;

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);

    int threadCount = (int)systemInfo.dwNumberOfProcessors;
    if (threadCount < 1)
        threadCount = 1;
    if (threadCount > blocksHigh)
        threadCount = blocksHigh;

    BC1Context ctx = {
        .srcRGBA = srcRGBA,
        .width = width,
        .blocksWide = blocksWide,
        .blocksHigh = blocksHigh,
        .dst = dst,
        .nextRow = 0
    };

    HANDLE* threads = malloc(sizeof(HANDLE) * threadCount);
    if (!threads) {
        BC1Worker(&ctx);
        return;
    }

    int created = 0;
    for (int i = 0; i < threadCount; i++) {
        HANDLE thread = CreateThread(NULL, 0, BC1Worker, &ctx, 0, NULL);
        if (thread)
            threads[created++] = thread;
    }

    if (created > 0) {
        WaitForMultipleObjects(created, threads, TRUE, INFINITE);
        for (int i = 0; i < created; i++)
            CloseHandle(threads[i]);
    }

    free(threads);
}

uint8_t* bc1TempBuffer = NULL;
uint8_t* bc7TempBuffer = NULL;
extern bool donePatching;

uint8_t* TC_CompressTexture(TrackedTexture* tex, const void* pSrcData, UINT* outRowPitch, UINT* outDepthPitch, D3D11_TEXTURE2D_DESC* textureDesc) {
    UINT bc1RowPitch    = ((textureDesc->Width + 3) / 4) * 8;
    UINT bc1DepthPitch  = bc1RowPitch * ((textureDesc->Height + 3) / 4);
    UINT bc7RowPitch    = ((textureDesc->Width + 3) / 4) * 16;
    UINT bc7DepthPitch  = bc7RowPitch * ((textureDesc->Height + 3) / 4);
    UINT RGBA8RowPitch  = textureDesc->Width * 4;
    UINT RGBA8DepthPitch = RGBA8RowPitch * textureDesc->Height;

    XXH64_hash_t hash = 0;
    uint8_t* targetBuffer = NULL;
    bool useBC1 = false;
    UINT targetRowPitch = 0;
    UINT targetDepthPitch = 0;
    if(EC_IsBC1Eligible(pSrcData, textureDesc->Width, textureDesc->Height, RGBA8DepthPitch, &hash)) {
        if(!bc1TempBuffer)
            bc1TempBuffer = malloc(bc1DepthPitch);
        if(!bc1TempBuffer)
            return NULL;
        useBC1 = true;
        targetBuffer = bc1TempBuffer;
        targetRowPitch = bc1RowPitch;
        targetDepthPitch = bc1DepthPitch;
    } else {
        if(!bc7TempBuffer)
            bc7TempBuffer = malloc(bc7DepthPitch);
        if(!bc7TempBuffer)
            return NULL;
        targetBuffer = bc7TempBuffer;
        targetRowPitch = bc7RowPitch;
        targetDepthPitch = bc7DepthPitch;
    }

    *outRowPitch = targetRowPitch;
    *outDepthPitch = targetDepthPitch;
    textureDesc->Format = useBC1 ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC7_UNORM;

    char searchPath[PATH_MAX];
    if(enableTextureCache) {
        snprintf(searchPath, PATH_MAX, "vsD3DHook_textureCache/%016" PRIX64 ".%s", hash, useBC1 ? "bc1" : "bc7");
        HANDLE hnd = CreateFileA(searchPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if(hnd != INVALID_HANDLE_VALUE)
        {
            printf("[D3DHook::TC] Found texture in cache! Reading...\n");
            DWORD bytesRead = 0;
            if(!ReadFile(hnd, targetBuffer, targetDepthPitch, &bytesRead, NULL) || bytesRead != targetDepthPitch)
            {
                printf("[D3DHook::TC] Read error!\n");
                CloseHandle(hnd);
            } else {
                CloseHandle(hnd);
                return targetBuffer;
            }
        }
    }

    // this is diabolical but i love it
    printf("[D3DHook::TC] Converting a texture atlas to %s...\n", useBC1 ? "BC1" : "BC7");
    (useBC1 ? compress_bc1 : ConvertRGBA8ToBC7)((const uint8_t*)pSrcData, textureDesc->Width, textureDesc->Height, (uint8_t*)targetBuffer);

    if(enableTextureCache) {
        CreateDirectoryA("vsD3DHook_textureCache", NULL);
        HANDLE hnd = CreateFileA(searchPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if(hnd != INVALID_HANDLE_VALUE) {
            if(!WriteFile(hnd, targetBuffer, targetDepthPitch, NULL, NULL))
                printf("[D3DHook::TC] Cannot write texture cache, WriteFile error!\n");
            CloseHandle(hnd);
        } else {
            printf("[D3DHook::TC] Cannot write texture cache, CreateFileA error!\n");
        }
    }

    return targetBuffer;
}

void TC_Dispose() {
    if(bc1TempBuffer != NULL) {
        printf("[D3DHook::TC] Freeing temporary BC1 memory...\n");
        free(bc1TempBuffer);
        bc1TempBuffer = NULL;
    }
    if(bc7TempBuffer != NULL) {
        printf("[D3DHook::TC] Freeing temporary BC7 memory...\n");
        free(bc7TempBuffer);
        bc7TempBuffer = NULL;
    }
}