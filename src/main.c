#include <stdio.h>
#include <stdint.h>
#include <windows.h>

#include "main.h"
#include "libs/bc7_wrapper.h"

#define false 0
#define true 1
#define bool char

bool started = false;
bool donePatching = false;

void CheckFormatSupport(ID3D11Device* device)
{
    UINT formatSupport = 0;
    HRESULT hr = ID3D11Device_CheckFormatSupport(device, DXGI_FORMAT_BC7_UNORM, &formatSupport);

    if (FAILED(hr)) {
        printf("[D3DHook] CheckFormatSupport call itself failed, hr=0x%08X\n", (unsigned)hr);
        MessageBoxA(NULL, "Function call to validate GPU format support failed!", "libGMDXHook Error!", MB_ICONERROR | MB_TOPMOST);
        ExitProcess(0);
        return;
    }

    if(!(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) || !(formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    {
        MessageBoxA(NULL, "Your GPU does not support this compression mode, sorry!", "libGMDXHook Error!", MB_ICONERROR | MB_TOPMOST);
        ExitProcess(0);
        return;
    }
}

#define MAX_TRACKED_TEXTURES 256
ID3D11Texture2D* trackedTextures[MAX_TRACKED_TEXTURES];
int trackedTextureCount = 0;

void TrackTexture(ID3D11Texture2D* tex) {
    if (trackedTextureCount < MAX_TRACKED_TEXTURES) {
        trackedTextures[trackedTextureCount++] = tex;
    }
}

bool IsTrackedTexture(ID3D11Resource* resource) {
    for (int i = 0; i < trackedTextureCount; i++) {
        if ((ID3D11Resource*)trackedTextures[i] == resource) {
            return true;
        }
    }
    return false;
}

PFN_CreateTexture2D realCreateTexture2D = NULL;
PFN_CreateShaderResourceView realCreateShaderResourceView = NULL;
PFN_UpdateSubresource realUpdateSubresource = NULL;

HRESULT STDMETHODCALLTYPE GM_CreateTexture2D(
    ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
{
    D3D11_TEXTURE2D_DESC modifiedDesc = *pDesc;
    bool wasModified = false;
    if (pDesc->Width == 4096 && pDesc->Height == 4096 && !donePatching)
    {
        wasModified = true;
        modifiedDesc.Format = DXGI_FORMAT_BC7_UNORM;
    }

    printf("[D3DHook] CreateTexture2D: %ux%u format=%u (%d) mips=%u arraySize=%u "
           "bindFlags=0x%X usage=%d cpuAccess=0x%X misc=0x%X hasInitialData=%s\n",
           pDesc->Width, pDesc->Height,
           pDesc->Format, pDesc->Format, pDesc->MipLevels, pDesc->ArraySize,
           pDesc->BindFlags, pDesc->Usage, pDesc->CPUAccessFlags, pDesc->MiscFlags,
           pInitialData ? "yes" : "no");

    HRESULT hr = realCreateTexture2D(This, &modifiedDesc, pInitialData, ppTexture2D);
    if(SUCCEEDED(hr) && wasModified && ppTexture2D && *ppTexture2D)
        TrackTexture(*ppTexture2D);

    return hr;
}

HRESULT STDMETHODCALLTYPE GM_CreateShaderResourceView(ID3D11Device* This, ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC modifiedDesc;
    const D3D11_SHADER_RESOURCE_VIEW_DESC* descToUse = pDesc;

    if (pDesc && IsTrackedTexture(pResource)) {
        modifiedDesc = *pDesc;
        modifiedDesc.Format = DXGI_FORMAT_BC7_UNORM;
        descToUse = &modifiedDesc;
        printf("[D3DHook] Adjusting SRV format for tracked texture %p\n", (void*)pResource);
    }

    return realCreateShaderResourceView(This, pResource, descToUse, ppSRView);
}

ID3D11DeviceContextVtbl* g_originalVtbl = NULL;
ID3D11DeviceContextVtbl* g_patchedVtbl = NULL;
ID3D11DeviceContext* g_patchedContext = NULL;
bool g_isPatched = false;

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

uint8_t* bc7Data = NULL;
UINT bc7RowPitch = (4096 / 4) * 16;
UINT bc7DepthPitch = 4096*4096;

void STDMETHODCALLTYPE GM_UpdateSubresource(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    if(pDstBox)
    {
        if(SrcDepthPitch == 67108864)
        {
            started = true;
            if(!bc7Data)
                bc7Data = malloc(4096 * 4096);
            printf("[D3DHook] Compressing texture atlas as BC7...\n");
            ConvertRGBA8ToBC7((const uint8_t*)pSrcData, 4096, 4096, bc7Data);
            realUpdateSubresource(This, pDstResource, DstSubresource, pDstBox, bc7Data, bc7RowPitch, bc7DepthPitch);
            return;
        }
        if(started && SrcDepthPitch != 67108864)
        {
            printf("[D3DHook] Restoring Device Context VTable...\n");
            donePatching = true;
            if(bc7Data)
                free(bc7Data);
            RestoreDeviceContextVtable();
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

HRESULT WINAPI GM_D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = D3D11CreateDevice(pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        printf("[D3DHook] Real device created at %p, patching vtable...\n", (void*)*ppDevice);
        PatchDeviceVtable(*ppDevice);
    }

    if (SUCCEEDED(hr) && ppImmediateContext && *ppImmediateContext) {
        printf("[D3DHook] Real device created at %p, patching context vtable...\n", (void*)*ppImmediateContext);
        PatchDeviceContextVtable(*ppImmediateContext);
    }

    return hr;
}