#pragma once

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class GBuffer
{
public:
    enum GBUFFER_TEXTURE_TYPE
    {
        GBUFFER_ALBEDO = 0,
        GBUFFER_NORMAL,
        GBUFFER_DEPTH,
        GBUFFER_COUNT
    };

    bool Initialize(ID3D12Device* device, UINT width, UINT height);
    void Shutdown();
    void OnResize(ID3D12Device* device, UINT width, UINT height);

    void CreateSrvs(ID3D12Device* device,
                    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
                    unsigned int gpuStartIndex,
                    unsigned int descriptorSize);

    ID3D12Resource* GetTexture(GBUFFER_TEXTURE_TYPE type) const { return mTextures[type].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(GBUFFER_TEXTURE_TYPE type) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(GBUFFER_TEXTURE_TYPE type) const;

    void ClearRenderTargets(
        ID3D12GraphicsCommandList* cmdList,
        const float* clearColorAlbedo = nullptr,
        const float* clearColorNormal = nullptr,
        const float* clearColorDepth = nullptr);

private:
    bool CreateTextures(ID3D12Device* device);
    bool CreateRTVs(ID3D12Device* device);

    ComPtr<ID3D12Resource> mTextures[GBUFFER_COUNT];
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;

    UINT mRtvDescriptorSize = 0;
    UINT mCbvSrvDescriptorSize = 0;
    UINT mWidth = 0;
    UINT mHeight = 0;

    static constexpr DXGI_FORMAT mAlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT mNormalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT mDepthFormat = DXGI_FORMAT_R32_FLOAT;
};
