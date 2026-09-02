#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "main.h"
#include "rootConfig.h"
#include "textureTracking.h"
#include "textureCompressor.h"
#include "eligibilityChecker.h"

PFN_CreateTexture2D realCreateTexture2D = NULL;
PFN_CreateShaderResourceView realCreateShaderResourceView = NULL;
PFN_UpdateSubresource realUpdateSubresource = NULL;

ID3D11DeviceContext* g_deviceContext = NULL;
ID3D11DeviceContextVtbl* g_originalVtbl = NULL;
ID3D11DeviceContextVtbl* g_patchedVtbl = NULL;
ID3D11DeviceContext* g_patchedContext = NULL;

bool g_isPatched = false;
bool started = false;
bool donePatching = false;

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

static UINT Align4(UINT v) {
    return (v + 3) & ~3u;
}

void STDMETHODCALLTYPE GM_UpdateSubresource(ID3D11DeviceContext* This, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    //printf("[D3DHook] Received UpdateSubresource for texture pointer %p\n", pDstResource);

    TrackedTexture* tex = ResolveResource(pDstResource);

    if(tex != NULL)
    {
        started = true;
        UINT outRowPitch;
        UINT outDepthPitch;
        uint8_t* buffer = TC_CompressTexture(tex, pSrcData, &outRowPitch, &outDepthPitch, &tex->originalDesc);
        if(buffer == NULL)
            return;
        
        if(experimentSupportNonAtlases) {
            tex->originalDesc.Width = Align4(tex->originalDesc.Width);
            tex->originalDesc.Height = Align4(tex->originalDesc.Height);
        }
        printf("[D3DHook] Creating real texture for texture handle %p (res: %ix%i)\n", (unsigned long long)tex->handle, tex->originalDesc.Width, tex->originalDesc.Height);
        HRESULT hr = realCreateTexture2D(tex->device, &tex->originalDesc, NULL, &tex->realTexture);
        if(SUCCEEDED(hr) && tex->realTexture)
        {
            printf("[D3DHook] Calling UpdateSubresource on texture handle %p (real: %p)\n", pDstResource, tex->realTexture);
            realUpdateSubresource(This, (ID3D11Resource*)tex->realTexture, DstSubresource, NULL, buffer, outRowPitch, outDepthPitch);
        }
    }
    if((started && tex == NULL) || (g_isPatched && donePatching))
    {
        printf("[D3DHook] Restoring Device Context VTable...\n");
        RestoreDeviceContextVtable();
        TC_Dispose();
        donePatching = true;
    }

    if(tex == NULL) // We can't pass a fake pointer
        realUpdateSubresource(This, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void PatchDeviceContextVtable(ID3D11DeviceContext* context)
{
    if (g_isPatched) {
        printf("[D3DHook] Context already patched, skipping\n");
        return;
    }

    g_originalVtbl = context->lpVtbl;

    g_patchedVtbl = malloc(sizeof(ID3D11DeviceContextVtbl));
    memcpy(g_patchedVtbl, g_originalVtbl, sizeof(ID3D11DeviceContextVtbl));

    realUpdateSubresource = g_originalVtbl->UpdateSubresource;
    g_patchedVtbl->UpdateSubresource = GM_UpdateSubresource;

    *(ID3D11DeviceContextVtbl**)context = g_patchedVtbl;

    g_patchedContext = context;
    g_isPatched = true;

    printf("[D3DHook] Context patched: %p -> %p\n", (void*)g_originalVtbl, (void*)g_patchedVtbl);
}

HRESULT STDMETHODCALLTYPE GM_CreateTexture2D(
    ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
{
    D3D11_TEXTURE2D_DESC modifiedDesc = *pDesc;
    bool shouldModify = false;
    if(pDesc->BindFlags == 0x8 && pDesc->CPUAccessFlags == 0x0 && pInitialData == NULL && pDesc->Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        if((pDesc->Width > 256 && pDesc->Height > 256) && (pDesc->Width == pDesc->Height || experimentSupportNonAtlases)) {
            shouldModify = true;
        }
    }

    printf("[D3DHook] CreateTexture2D: %ux%u format=%u (%d) mips=%u arraySize=%u "
           "bindFlags=0x%X usage=%d cpuAccess=0x%X misc=0x%X hasInitialData=%s\n",
           pDesc->Width, pDesc->Height,
           pDesc->Format, pDesc->Format, pDesc->MipLevels, pDesc->ArraySize,
           pDesc->BindFlags, pDesc->Usage, pDesc->CPUAccessFlags, pDesc->MiscFlags,
           pInitialData ? "yes" : "no");

    if(!shouldModify) {
        return realCreateTexture2D(This, pDesc, pInitialData, ppTexture2D);
    }

    if(!g_isPatched) {
        printf("[D3DHook] Eligible texture loaded, patching Device Context VTable...\n");
        PatchDeviceContextVtable(g_deviceContext);
    }

    uint64_t handle = TrackTexture(This, pDesc);
    if(handle == 0)
        return E_ABORT;
    *ppTexture2D = (ID3D11Texture2D*)(uintptr_t)handle;

    printf("[D3DHook] Returning fake CreateTexture2D pointer: %p\n", (void*)*ppTexture2D);

    return S_OK;
}

HRESULT STDMETHODCALLTYPE GM_CreateShaderResourceView(ID3D11Device* This, ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC modifiedDesc;
    const D3D11_SHADER_RESOURCE_VIEW_DESC* descToUse = pDesc;

    //printf("[D3DHook] Received CreateShaderResourceView for texture pointer %p\n", pResource);

    TrackedTexture* tex = ResolveResource(pResource);
    if(tex == NULL) {
        return realCreateShaderResourceView(This, pResource, descToUse, ppSRView);
    }

    modifiedDesc = *pDesc;
    modifiedDesc.Format = tex->originalDesc.Format;
    descToUse = &modifiedDesc;

    printf("[D3DHook] Tweaking SRV format for texture handle %p\n", pResource);
    return realCreateShaderResourceView(This, (ID3D11Resource*)tex->realTexture, descToUse, ppSRView);
}

void PatchDeviceVtable(ID3D11Device* device) {
    void** vtable = *(void***)device;

    DWORD oldProtect;
    VirtualProtect(&vtable[5], sizeof(void*), PAGE_READWRITE, &oldProtect);

    realCreateTexture2D = (PFN_CreateTexture2D)vtable[5];
    vtable[5] = (void*)GM_CreateTexture2D;
    realCreateShaderResourceView = (PFN_CreateShaderResourceView)vtable[7];
    vtable[7] = (void*)GM_CreateShaderResourceView;

    VirtualProtect(&vtable[5], sizeof(void*), oldProtect, &oldProtect);

    printf("[D3DHook] Patched CreateTexture2D vtable slot (was %p, now %p)\n", (void*)realCreateTexture2D, (void*)GM_CreateTexture2D);
    printf("[D3DHook] Patched CreateShaderResourceView vtable slot (was %p, now %p)\n", (void*)realCreateShaderResourceView, (void*)GM_CreateShaderResourceView);
}

HRESULT WINAPI GM_D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
    EC_Init();
    HRESULT hr = D3D11CreateDevice(pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        printf("[D3DHook] Real device created at %p, patching vtable...\n", (void*)*ppDevice);
        PatchDeviceVtable(*ppDevice);
    }

    if (SUCCEEDED(hr) && ppImmediateContext && *ppImmediateContext) {
        printf("[D3DHook] Real context created at %p\n", (void*)*ppImmediateContext);
        g_deviceContext = *ppImmediateContext;
    }

    return hr;
}