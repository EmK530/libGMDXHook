#include "textureTracking.h"

#include <stddef.h>

static int trackedTextureCount = 0;

static TrackedTexture trackedTextures[MAX_TRACKED_TEXTURES];

static uint64_t nextTextureHandle = TEXTURE_HANDLE_BASE;

static int IsTextureHandle(uint64_t handle) {
    return (handle & TEXTURE_HANDLE_MASK) == TEXTURE_HANDLE_BASE;
}

uint64_t TrackTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* originalDesc) {
    if (trackedTextureCount >= MAX_TRACKED_TEXTURES)
        return 0;
    uint64_t handle = nextTextureHandle++;
    TrackedTexture* texture = &trackedTextures[trackedTextureCount++];
    texture->handle = handle;
    texture->device = device;
    texture->originalDesc = *originalDesc;
    texture->realTexture = NULL;
    if (device)
        device->lpVtbl->AddRef(device);
    return handle;
}


TrackedTexture* ResolveTexture(uint64_t handle) {
    if (!IsTextureHandle(handle))
        return NULL;
    for (int i = 0; i < trackedTextureCount; i++) {
        if (trackedTextures[i].handle == handle)
            return &trackedTextures[i];
    }
    return NULL;
}


TrackedTexture* ResolveResource(ID3D11Resource* resource) {
    if (!resource)
        return NULL;
    return ResolveTexture((uint64_t)(uintptr_t)resource);
}


void UntrackTexture(uint64_t handle) {
    TrackedTexture* texture = ResolveTexture(handle);
    if (!texture)
        return;

    if (texture->realTexture) {
        texture->realTexture->lpVtbl->Release(texture->realTexture);
        texture->realTexture = NULL;
    }
    if (texture->device) {
        texture->device->lpVtbl->Release(texture->device);
        texture->device = NULL;
    }

    int index = (int)(texture - trackedTextures);
    trackedTextures[index] = trackedTextures[trackedTextureCount - 1];
    trackedTextureCount--;
}