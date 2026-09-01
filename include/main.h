#pragma once

#define COBJMACROS
#include <d3d11.h>

typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateTexture2D)(
    ID3D11Device* This, const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateShaderResourceView)(
    ID3D11Device* This, ID3D11Resource* pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView);
typedef void (STDMETHODCALLTYPE *PFN_UpdateSubresource)(
    ID3D11DeviceContext *This,
    ID3D11Resource *pDstResource,
    UINT DstSubresource,
    const D3D11_BOX *pDstBox,
    const void *pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch);