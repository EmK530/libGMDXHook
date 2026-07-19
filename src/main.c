#include <stdio.h>
#include <stdint.h>
#include <windows.h>
#include <inttypes.h>

#include "main.h"

#ifndef TEXTURE_MODE
    #define TEXTURE_MODE 1
#endif

#if TEXTURE_MODE == 1
    #include "libs/bc7_wrapper.h"
    #ifndef ALLOW_CACHING
        #define ALLOW_CACHING 1
    #endif
    #if ALLOW_CACHING == 1
        #define XXH_STATIC_LINKING_ONLY
        #define XXH_IMPLEMENTATION
        #include "external/xxhash/xxhash.h"
    #endif
#endif
#if TEXTURE_MODE == 2
    #include "libs/b4g4r4a4_convert.h"
#endif

#define false 0
#define true 1
#define bool char

#if TEXTURE_MODE == 1
    static enum DXGI_FORMAT formatToConvertTo = DXGI_FORMAT_BC7_UNORM;
#endif
#if TEXTURE_MODE == 2
    static enum DXGI_FORMAT formatToConvertTo = DXGI_FORMAT_B4G4R4A4_UNORM;
#endif

bool started = false;
bool donePatching = false;

bool CheckFormatSupport(ID3D11Device* device)
{
    UINT formatSupport = 0;
    HRESULT hr = ID3D11Device_CheckFormatSupport(device, formatToConvertTo, &formatSupport);

    if (FAILED(hr)) {
        printf("[D3DHook] CheckFormatSupport call itself failed, hr=0x%08X\n", (unsigned)hr);
        MessageBoxA(NULL, "Function call to validate GPU format support failed!", "libGMDXHook Error!", MB_ICONERROR | MB_TOPMOST);
        return false;
    }

    if(!(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) || !(formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    {
        MessageBoxA(NULL, "Your GPU does not support this compression mode, sorry!", "libGMDXHook Error!", MB_ICONERROR | MB_TOPMOST);
        return false;
    }

    return true;
}

#define MAX_TRACKED_TEXTURES 256
TrackedTexture trackedTextures[MAX_TRACKED_TEXTURES];

int trackedTextureCount = 0;

void TrackTexture(ID3D11Texture2D* tex, int width, int height) {
    if (trackedTextureCount < MAX_TRACKED_TEXTURES) {
        trackedTextures[trackedTextureCount++] = (TrackedTexture){.texturePointer = tex, .textureWidth = width, .textureHeight = height};
    }
}

TrackedTexture* IsTrackedTexture(ID3D11Resource* resource) {
    TrackedTexture* tex = trackedTextures;
    for (int i = 0; i < trackedTextureCount; i++) {
        if ((ID3D11Resource*)(tex->texturePointer) == resource) {
            return tex;
        }
        tex++;
    }
    return NULL;
}

PFN_CreateTexture2D realCreateTexture2D = NULL;
PFN_CreateShaderResourceView realCreateShaderResourceView = NULL;
PFN_UpdateSubresource realUpdateSubresource = NULL;

ID3D11DeviceContext* g_deviceContext = NULL;
ID3D11DeviceContextVtbl* g_originalVtbl = NULL;
ID3D11DeviceContextVtbl* g_patchedVtbl = NULL;
ID3D11DeviceContext* g_patchedContext = NULL;
bool g_isPatched = false;

void STDMETHODCALLTYPE GM_UpdateSubresource(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);

void PatchDeviceContextVtable(ID3D11DeviceContext* context)
{
    if (g_isPatched) {
        printf("[D3DHook] Context already patched, skipping\n");
        return;
    }

    g_originalVtbl = context->lpVtbl; // save the REAL, driver-owned vtable pointer

    g_patchedVtbl = malloc(sizeof(ID3D11DeviceContextVtbl));
    memcpy(g_patchedVtbl, g_originalVtbl, sizeof(ID3D11DeviceContextVtbl));

    realUpdateSubresource = g_originalVtbl->UpdateSubresource;
    g_patchedVtbl->UpdateSubresource = GM_UpdateSubresource;

    *(ID3D11DeviceContextVtbl**)context = g_patchedVtbl;

    g_patchedContext = context;
    g_isPatched = true;

    printf("[D3DHook] Context patched: %p -> %p\n", (void*)g_originalVtbl, (void*)g_patchedVtbl);
}

void RestoreDeviceContextVtable(void)
{
    if (!g_isPatched || !g_patchedContext) {
        printf("[D3DHook] Nothing to restore\n");
        return;
    }

    *(ID3D11DeviceContextVtbl**)g_patchedContext = g_originalVtbl;

    printf("[D3DHook] Context restored to original vtable %p\n", (void*)g_originalVtbl);

    free(g_patchedVtbl);
    g_patchedVtbl = NULL;
    g_patchedContext = NULL;
    g_isPatched = false;
}

HRESULT STDMETHODCALLTYPE GM_CreateTexture2D(
    ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
{
    D3D11_TEXTURE2D_DESC modifiedDesc = *pDesc;
    bool wasModified = false;
    if ((pDesc->Width == 2048 || pDesc->Width == 4096) && pDesc->Width == pDesc->Height && pDesc->Format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        wasModified = true;
        modifiedDesc.Format = formatToConvertTo;
    }

    printf("[D3DHook] CreateTexture2D: %ux%u format=%u (%d) mips=%u arraySize=%u "
           "bindFlags=0x%X usage=%d cpuAccess=0x%X misc=0x%X hasInitialData=%s\n",
           pDesc->Width, pDesc->Height,
           pDesc->Format, pDesc->Format, pDesc->MipLevels, pDesc->ArraySize,
           pDesc->BindFlags, pDesc->Usage, pDesc->CPUAccessFlags, pDesc->MiscFlags,
           pInitialData ? "yes" : "no");

    HRESULT hr = realCreateTexture2D(This, &modifiedDesc, pInitialData, ppTexture2D);
    if(SUCCEEDED(hr) && wasModified && ppTexture2D && *ppTexture2D)
    {
        TrackTexture(*ppTexture2D, pDesc->Width, pDesc->Height);
        if(!g_isPatched && g_deviceContext)
        {
            printf("[D3DHook] Atlas loaded into VRAM post-patch! Re-hooking...\n");
            PatchDeviceContextVtable(g_deviceContext);
        }
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE GM_CreateShaderResourceView(ID3D11Device* This, ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC modifiedDesc;
    const D3D11_SHADER_RESOURCE_VIEW_DESC* descToUse = pDesc;

    if (pDesc && IsTrackedTexture(pResource)) {
        modifiedDesc = *pDesc;
        modifiedDesc.Format = formatToConvertTo;
        descToUse = &modifiedDesc;
        printf("[D3DHook] Adjusting SRV format for tracked texture %p\n", (void*)pResource);
    }

    return realCreateShaderResourceView(This, pResource, descToUse, ppSRView);
}

uint8_t* converterBuffer = NULL;

#if TEXTURE_MODE == 1 && ALLOW_CACHING == 1
    bool directoryNeverExisted = false;
    bool hasPerformedDirectoryCheck = false;

    bool DirectoryExists(const char* path)
    {
        DWORD attrs = GetFileAttributesA(path);

        if (attrs == INVALID_FILE_ATTRIBUTES) {
            return false;
        }

        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    char seenHashes[MAX_TRACKED_TEXTURES][33];
    int seenHashCount = 0;

    void TrackSeenHash(const char* hashHex)
    {
        if (seenHashCount < MAX_TRACKED_TEXTURES) {
            strncpy(seenHashes[seenHashCount], hashHex, 32);
            seenHashes[seenHashCount][32] = '\0';
            seenHashCount++;
        }
    }

    bool WasHashSeen(const char* hashHex)
    {
        for (int i = 0; i < seenHashCount; i++) {
            if (strncmp(seenHashes[i], hashHex, 32) == 0) {
                return true;
            }
        }
        return false;
    }

    void PruneStaleCacheEntries(void)
    {
        printf("[D3DHook] Pruning stale cache entries...\n");

        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA("libGMDXHook_textureCache\\*.bc7", &findData);

        if (hFind == INVALID_HANDLE_VALUE) {
            printf("[D3DHook] No cache files found to prune\n");
            return;
        }

        int prunedCount = 0;

        do {
            char hashHex[33];
            size_t nameLen = strlen(findData.cFileName);
            if (nameLen != 36) {
                continue; // not a file we recognize the naming pattern of
            }
            strncpy(hashHex, findData.cFileName, 32);
            hashHex[32] = '\0';

            if (!WasHashSeen(hashHex)) {
                char fullPath[MAX_PATH];
                snprintf(fullPath, sizeof(fullPath), "libGMDXHook_textureCache\\%s", findData.cFileName);
                if (DeleteFileA(fullPath)) {
                    printf("[D3DHook] Pruned stale cache entry: %s\n", findData.cFileName);
                    prunedCount++;
                } else {
                    printf("[D3DHook] Failed to delete stale entry %s, err=%lu\n", findData.cFileName, GetLastError());
                }
            }
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);

        printf("[D3DHook] Pruning complete, removed %d stale entries\n", prunedCount);
    }

    BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
    {
        switch (fdwReason)
        {
            case DLL_PROCESS_DETACH:
                printf("[D3DHook] Process ending, cleanup time!\n");
                PruneStaleCacheEntries();
                break;
            }
        return TRUE;
    }
#endif

void STDMETHODCALLTYPE GM_UpdateSubresource(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    if(pDstBox)
    {
        TrackedTexture* tex = IsTrackedTexture(pDstResource);
        if(tex != NULL)
        {
            UINT bc7RowPitch    = ((tex->textureWidth + 3) / 4) * 16;
            UINT bc7DepthPitch  = bc7RowPitch * ((tex->textureHeight + 3) / 4);
            UINT b4RowPitch     = tex->textureWidth * 2;
            UINT b4DepthPitch   = b4RowPitch * tex->textureHeight;
            UINT RGBA8RowPitch  = tex->textureWidth * 4;
            UINT RGBA8DepthPitch = RGBA8RowPitch * tex->textureHeight;

            #if TEXTURE_MODE == 1 && ALLOW_CACHING == 1
                char searchPath[MAX_PATH];
                XXH128_hash_t hash = XXH3_128bits(pSrcData, SrcDepthPitch);
                char hashHex[33];
                snprintf(hashHex, sizeof(hashHex), "%016" PRIX64 "%016" PRIX64, hash.high64, hash.low64);
                TrackSeenHash(hashHex);
                printf("[D3DHook] Hash of incoming texture data is %s\n", hashHex);
                snprintf(searchPath, MAX_PATH, "libGMDXHook_textureCache/%s.bc7", hashHex);
                if(!hasPerformedDirectoryCheck)
                {
                    hasPerformedDirectoryCheck = true;
                    if(!DirectoryExists("libGMDXHook_textureCache"))
                    {
                        directoryNeverExisted = true;
                        CreateDirectoryA("libGMDXHook_textureCache", NULL);
                    }
                }
                if(directoryNeverExisted)
                    goto convertRealtime;
                HANDLE hnd = CreateFileA(searchPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if(hnd != INVALID_HANDLE_VALUE)
                {
                    printf("[D3DHook] Found texture in cache! Reading...\n");
                    if(!converterBuffer)
                        converterBuffer = malloc(bc7DepthPitch);
                    DWORD bytesRead = 0;
                    if(!ReadFile(hnd, converterBuffer, bc7DepthPitch, &bytesRead, NULL) || bytesRead != bc7DepthPitch)
                    {
                        printf("[D3DHook] Read error!\n");
                        CloseHandle(hnd);
                        goto convertRealtime;
                    } else {
                        CloseHandle(hnd);
                        started = true;
                        realUpdateSubresource(This, pDstResource, DstSubresource, pDstBox, converterBuffer, bc7RowPitch, bc7DepthPitch);
                        if(donePatching)
                            goto restoreDevice;
                        return;
                    }
                }
            #endif
            convertRealtime:
            started = true;
#if TEXTURE_MODE == 1
            if(!converterBuffer)
                converterBuffer = malloc(bc7DepthPitch);
            printf("[D3DHook] Compressing texture atlas as BC7...\n");
            ConvertRGBA8ToBC7((const uint8_t*)pSrcData, tex->textureWidth, tex->textureHeight, converterBuffer);
            realUpdateSubresource(This, pDstResource, DstSubresource, pDstBox, converterBuffer, bc7RowPitch, bc7DepthPitch);
        #if ALLOW_CACHING == 1
            printf("%s\n", searchPath);
            HANDLE hnd2 = CreateFileA(searchPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if(hnd2 != INVALID_HANDLE_VALUE) {
                if(!WriteFile(hnd2, converterBuffer, bc7DepthPitch, NULL, NULL))
                    printf("[D3DHook] Cannot write texture cache, WriteFile error!\n");
                CloseHandle(hnd2);
            } else {
                printf("[D3DHook] Cannot write texture cache, CreateFileA error!\n");
            }
        #endif
            if(donePatching)
                goto restoreDevice;
#endif
#if TEXTURE_MODE == 2
            if(!converterBuffer)
                converterBuffer = malloc(b4DepthPitch);
            printf("[D3DHook] Converting texture atlas to B4G4R4A4...\n");
            ConvertRGBA8ToB4G4R4A4((const uint8_t*)pSrcData, 4096, 4096, (uint16_t*)converterBuffer);
            realUpdateSubresource(This, pDstResource, DstSubresource, pDstBox, converterBuffer, b4RowPitch, b4DepthPitch);
#endif
            return;
        }
        if(started && tex == NULL)
        {
            restoreDevice:
            printf("[D3DHook] Restoring Device Context VTable...\n");
            if(converterBuffer)
            {
                free(converterBuffer);
                converterBuffer = NULL;
            }
            RestoreDeviceContextVtable();
            if(donePatching) {
                return;
            } else {
                donePatching = true;
            }
        }
    }

    realUpdateSubresource(This, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void PatchDeviceVtable(ID3D11Device* device) {
    void** vtable = *(void***)device; // vtable pointer is always the first field of a COM object

    DWORD oldProtect;
    VirtualProtect(&vtable[5], sizeof(void*), PAGE_READWRITE, &oldProtect);

    realCreateTexture2D = (PFN_CreateTexture2D)vtable[5];
    vtable[5] = (void*)GM_CreateTexture2D;
    realCreateShaderResourceView = (PFN_CreateShaderResourceView)vtable[7];
    vtable[7] = (void*)GM_CreateShaderResourceView;

    VirtualProtect(&vtable[5], sizeof(void*), oldProtect, &oldProtect);

    printf("[D3DHook] Patched CreateTexture2D vtable slot (was %p, now %p)\n",
           (void*)realCreateTexture2D, (void*)GM_CreateTexture2D);
}

HRESULT WINAPI GM_D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = D3D11CreateDevice(pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        if(!CheckFormatSupport(*ppDevice))
            return E_FAIL;
        printf("[D3DHook] Real device created at %p, patching vtable...\n", (void*)*ppDevice);
        PatchDeviceVtable(*ppDevice);
    }

    if (SUCCEEDED(hr) && ppImmediateContext && *ppImmediateContext) {
        g_deviceContext = *ppImmediateContext;
        printf("[D3DHook] Real device created at %p, patching context vtable...\n", (void*)*ppImmediateContext);
        PatchDeviceContextVtable(*ppImmediateContext);
    }

    return hr;
}