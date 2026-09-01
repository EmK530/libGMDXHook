#include <stdio.h>

#include "textureCompressor.h"

#define STB_DXT_IMPLEMENTATION
#include "external/stb/stb_dxt.h"

void compress_bc1(const uint8_t* srcRGBA, int width, int height, uint8_t* dst)
{
    const int blocksWide = width / 4;
    const int blocksHigh = height / 4;

    #pragma omp parallel for collapse(2)
    for (int by = 0; by < blocksHigh; by++)
    {
        for (int bx = 0; bx < blocksWide; bx++)
        {
            uint8_t block[64];
            for (int y = 0; y < 4; y++) {
                const uint8_t* srcRow = srcRGBA + ((by * 4 + y) * width + bx * 4) * 4;
                memcpy(block + y * 16, srcRow, 16);
            }
            const size_t blockIndex = (size_t)by * blocksWide + bx;
            stb__CompressBC1AlphaBlock(dst + blockIndex * 8, block, STB_DXT_HIGHQUAL);
        }
    }
}

uint8_t* bc1TempBuffer = NULL;
uint8_t* bc7TempBuffer = NULL;

uint8_t* TC_CompressTexture(TrackedTexture* tex, const void* pSrcData, UINT* outRowPitch, UINT* outDepthPitch, D3D11_TEXTURE2D_DESC* textureDesc) {
    UINT bc1RowPitch    = ((textureDesc->Width + 3) / 4) * 8;
    UINT bc1DepthPitch  = bc1RowPitch * ((textureDesc->Height + 3) / 4);
    UINT bc7RowPitch    = ((textureDesc->Width + 3) / 4) * 16;
    UINT bc7DepthPitch  = bc7RowPitch * ((textureDesc->Height + 3) / 4);

    if(!bc1TempBuffer)
        bc1TempBuffer = malloc(bc1DepthPitch);
    if(!bc1TempBuffer)
        return NULL;

    printf("[D3DHook::TC] Converting a texture atlas to BC1...\n");
    textureDesc->Format = DXGI_FORMAT_BC1_UNORM;
    compress_bc1((const uint8_t*)pSrcData, textureDesc->Width, textureDesc->Height, (uint8_t*)bc1TempBuffer);
    *outRowPitch = bc1RowPitch;
    *outDepthPitch = bc1DepthPitch;
    return bc1TempBuffer;
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