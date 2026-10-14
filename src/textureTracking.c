#include "textureTracking.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int trackedTextureCount = 0;

TrackedTexture* trackedTextures = NULL;

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

typedef struct
{
    const ID3D11Texture2DVtbl* lpVtbl;
    LONG refCount;
} TextureProxy;

// Spelled out here so linking doesn't need dxguid.
static const GUID kIID_IUnknown        = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIID_DeviceChild     = {0x1841e5c8, 0x16b0, 0x489b, {0xbc, 0xc8, 0x44, 0xcf, 0xb0, 0xd5, 0xde, 0xae}};
static const GUID kIID_Resource        = {0xdc8e63f3, 0xd12b, 0x4952, {0xb4, 0x7b, 0x5e, 0x45, 0x02, 0x6a, 0x86, 0x2d}};
static const GUID kIID_Texture2D       = {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

#define PROXY_HANDLE(This) ((uint64_t)(uintptr_t)(This))
#define PROXY_REAL(This) (ResolveTexture(PROXY_HANDLE(This)) ? ResolveTexture(PROXY_HANDLE(This))->realTexture : NULL)

static HRESULT STDMETHODCALLTYPE Proxy_QueryInterface(ID3D11Texture2D* This, REFIID riid, void** ppvObject) {
    if (!ppvObject)
        return E_POINTER;

    if (IsEqualGUID(riid, &kIID_IUnknown) || IsEqualGUID(riid, &kIID_DeviceChild) ||
        IsEqualGUID(riid, &kIID_Resource) || IsEqualGUID(riid, &kIID_Texture2D)) {
        This->lpVtbl->AddRef(This);
        *ppvObject = This;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Proxy_AddRef(ID3D11Texture2D* This) {
    return (ULONG)InterlockedIncrement(&((TextureProxy*)This)->refCount);
}

static ULONG STDMETHODCALLTYPE Proxy_Release(ID3D11Texture2D* This) {
    TextureProxy* proxy = (TextureProxy*)This;
    ULONG refs = (ULONG)InterlockedDecrement(&proxy->refCount);
    if (refs == 0) {
        printf("[D3DHook] Game released texture handle %p, freeing real texture\n", (void*)proxy);
        UntrackTexture(PROXY_HANDLE(proxy));
        free(proxy);
    }
    return refs;
}

static void STDMETHODCALLTYPE Proxy_GetDevice(ID3D11Texture2D* This, ID3D11Device** ppDevice) {
    TrackedTexture* tex = ResolveTexture(PROXY_HANDLE(This));
    *ppDevice = tex ? tex->device : NULL;
    if (*ppDevice)
        (*ppDevice)->lpVtbl->AddRef(*ppDevice);
}

static HRESULT STDMETHODCALLTYPE Proxy_GetPrivateData(ID3D11Texture2D* This, REFGUID guid, UINT* pDataSize, void* pData) {
    ID3D11Texture2D* real = PROXY_REAL(This);
    if (real)
        return real->lpVtbl->GetPrivateData(real, guid, pDataSize, pData);
    if (pDataSize)
        *pDataSize = 0;
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE Proxy_SetPrivateData(ID3D11Texture2D* This, REFGUID guid, UINT DataSize, const void* pData) {
    ID3D11Texture2D* real = PROXY_REAL(This);
    return real ? real->lpVtbl->SetPrivateData(real, guid, DataSize, pData) : S_OK;
}

static HRESULT STDMETHODCALLTYPE Proxy_SetPrivateDataInterface(ID3D11Texture2D* This, REFGUID guid, const IUnknown* pData) {
    ID3D11Texture2D* real = PROXY_REAL(This);
    return real ? real->lpVtbl->SetPrivateDataInterface(real, guid, pData) : S_OK;
}

static void STDMETHODCALLTYPE Proxy_GetType(ID3D11Texture2D* This, D3D11_RESOURCE_DIMENSION* pResourceDimension) {
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
}

static void STDMETHODCALLTYPE Proxy_SetEvictionPriority(ID3D11Texture2D* This, UINT EvictionPriority) {
    ID3D11Texture2D* real = PROXY_REAL(This);
    if (real)
        real->lpVtbl->SetEvictionPriority(real, EvictionPriority);
}

static UINT STDMETHODCALLTYPE Proxy_GetEvictionPriority(ID3D11Texture2D* This) {
    ID3D11Texture2D* real = PROXY_REAL(This);
    return real ? real->lpVtbl->GetEvictionPriority(real) : DXGI_RESOURCE_PRIORITY_NORMAL;
}

static void STDMETHODCALLTYPE Proxy_GetDesc(ID3D11Texture2D* This, D3D11_TEXTURE2D_DESC* pDesc) {
    TrackedTexture* tex = ResolveTexture(PROXY_HANDLE(This));
    if (!tex) {
        memset(pDesc, 0, sizeof(*pDesc));
        return;
    }
    *pDesc = tex->originalDesc;
    pDesc->Format = DXGI_FORMAT_R8G8B8A8_UNORM;
}

static const ID3D11Texture2DVtbl g_proxyVtbl = {
    .QueryInterface = Proxy_QueryInterface,
    .AddRef = Proxy_AddRef,
    .Release = Proxy_Release,
    .GetDevice = Proxy_GetDevice,
    .GetPrivateData = Proxy_GetPrivateData,
    .SetPrivateData = Proxy_SetPrivateData,
    .SetPrivateDataInterface = Proxy_SetPrivateDataInterface,
    .GetType = Proxy_GetType,
    .SetEvictionPriority = Proxy_SetEvictionPriority,
    .GetEvictionPriority = Proxy_GetEvictionPriority,
    .GetDesc = Proxy_GetDesc,
};

uint64_t TrackTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* originalDesc) {
    if (!hashTableInitialized)
        HashTableInit();

    if (trackedTextureCount >= MAX_TRACKED_TEXTURES)
        return 0;

    TextureProxy* proxy = malloc(sizeof(TextureProxy));
    if (!proxy)
        return 0;
    proxy->lpVtbl = &g_proxyVtbl;
    proxy->refCount = 1;

    uint64_t handle = PROXY_HANDLE(proxy);
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
    if (!hashTableInitialized)
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