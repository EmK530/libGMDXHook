#pragma once

#include <stdint.h>
#include <d3d11.h>

#define MAX_TRACKED_TEXTURES 2048

typedef struct
{
    uint64_t handle;
    ID3D11Device* device;
    D3D11_TEXTURE2D_DESC originalDesc;
    ID3D11Texture2D* realTexture;
} TrackedTexture;

uint64_t TrackTexture(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC* originalDesc
);

TrackedTexture* ResolveTexture(uint64_t handle);

TrackedTexture* ResolveResource(ID3D11Resource* resource);

void UntrackTexture(uint64_t handle);