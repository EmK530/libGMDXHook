#include "textureTracking.h"

#include <stddef.h>
#include <stdint.h>

static int trackedTextureCount = 0;

TrackedTexture* trackedTextures = NULL;

static uint64_t nextTextureHandle = TEXTURE_HANDLE_BASE;

#define HASH_EMPTY    (-1)
#define HASH_TOMBSTONE (-2)

#define NEXT_POW2_HELPER(x) ((x) | ((x) >> 1) | ((x) >> 2) | ((x) >> 4) | ((x) >> 8) | ((x) >> 16))
#define ROUND_UP_POW2(x) (NEXT_POW2_HELPER((x) - 1) + 1)

#define HASH_CAPACITY ROUND_UP_POW2(MAX_TRACKED_TEXTURES * 2)

int32_t* hashTable = NULL;
static int hashTableInitialized = 0;

static void HashTableInit(void) {
    hashTable = malloc(sizeof(int32_t) * HASH_CAPACITY);
    for (int i = 0; i < HASH_CAPACITY; i++)
        hashTable[i] = HASH_EMPTY;
    hashTableInitialized = 1;
    trackedTextures = malloc(sizeof(TrackedTexture) * MAX_TRACKED_TEXTURES);
}

static uint32_t HashHandle(uint64_t handle) {
    uint64_t h = handle;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (uint32_t)(h & (HASH_CAPACITY - 1));
}

static void HashInsert(uint64_t handle, int index) {
    uint32_t pos = HashHandle(handle);
    while (hashTable[pos] != HASH_EMPTY && hashTable[pos] != HASH_TOMBSTONE) {
        pos = (pos + 1) & (HASH_CAPACITY - 1);
    }
    hashTable[pos] = index;
}

static int HashFind(uint64_t handle) {
    uint32_t pos = HashHandle(handle);
    uint32_t start = pos;
    while (hashTable[pos] != HASH_EMPTY) {
        if (hashTable[pos] != HASH_TOMBSTONE && trackedTextures[hashTable[pos]].handle == handle)
            return hashTable[pos];
        pos = (pos + 1) & (HASH_CAPACITY - 1);
        if (pos == start)
            break;
    }
    return -1;
}

static void HashRemove(uint64_t handle) {
    uint32_t pos = HashHandle(handle);
    uint32_t start = pos;
    while (hashTable[pos] != HASH_EMPTY) {
        if (hashTable[pos] != HASH_TOMBSTONE && trackedTextures[hashTable[pos]].handle == handle) {
            hashTable[pos] = HASH_TOMBSTONE;
            return;
        }
        pos = (pos + 1) & (HASH_CAPACITY - 1);
        if (pos == start)
            break;
    }
}

static void HashUpdateIndex(uint64_t handle, int newIndex) {
    uint32_t pos = HashHandle(handle);
    uint32_t start = pos;
    while (hashTable[pos] != HASH_EMPTY) {
        if (hashTable[pos] != HASH_TOMBSTONE && trackedTextures[hashTable[pos]].handle == handle) {
            hashTable[pos] = newIndex;
            return;
        }
        pos = (pos + 1) & (HASH_CAPACITY - 1);
        if (pos == start)
            break;
    }
}

static int IsTextureHandle(uint64_t handle) {
    return (handle & TEXTURE_HANDLE_MASK) == TEXTURE_HANDLE_BASE;
}

uint64_t TrackTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* originalDesc) {
    if (!hashTableInitialized)
        HashTableInit();

    if (trackedTextureCount >= MAX_TRACKED_TEXTURES)
        return 0;

    uint64_t handle = nextTextureHandle++;
    int index = trackedTextureCount++;
    TrackedTexture* texture = &trackedTextures[index];
    texture->handle = handle;
    texture->device = device;
    texture->originalDesc = *originalDesc;
    texture->realTexture = NULL;
    if (device)
        device->lpVtbl->AddRef(device);

    HashInsert(handle, index);

    return handle;
}


TrackedTexture* ResolveTexture(uint64_t handle) {
    if (!IsTextureHandle(handle))
        return NULL;
    int index = HashFind(handle);
    if (index < 0)
        return NULL;
    return &trackedTextures[index];
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
    int lastIndex = trackedTextureCount - 1;

    HashRemove(handle);

    if (index != lastIndex) {
        uint64_t movedHandle = trackedTextures[lastIndex].handle;
        trackedTextures[index] = trackedTextures[lastIndex];
        HashUpdateIndex(movedHandle, index);
    }

    trackedTextureCount--;
}