#pragma once

#include <stdint.h>

#include "textureTracking.h"

uint8_t* TC_CompressTexture(TrackedTexture* tex, const void* pSrcData, UINT* outRowPitch, UINT* outDepthPitch, D3D11_TEXTURE2D_DESC* textureDesc);

void TC_Dispose();