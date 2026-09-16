#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <vector>
#include <wrl/client.h>
#include "CameraConstants.h"
#include "GBuffer.h"
#include "Light.h"
#include "Material.h"
#include "PostConstants.h"
#include "Submesh.h"
#include "UploadBuffer.h"

using Microsoft::WRL::ComPtr;

class RenderingSystem
{
public:
    RenderingSystem(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        ID3D12GraphicsCommandList* commandList,
        ID3D12CommandAllocator* commandAllocator,
        ID3D12Fence* fence,
        UINT swapChainBufferCount,
        DXGI_FORMAT backBufferFormat);

    ~RenderingSystem();

    bool Initialize(UINT width, UINT height);
    void Shutdown();
    void FlushCommandQueue();

    void GeometryPass(
        ID3D12PipelineState* geometryPSO,
        ID3D12RootSignature* geometryRootSignature,
        ID3D12DescriptorHeap* sceneHeap,
        UINT descriptorSize,
        UINT materialSrvOffset,
        const std::vector<Submesh>& submeshes,
        const std::vector<Material>& materials,
        const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
        const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
        ID3D12Resource* depthStencilBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect);

    void LightingPass(
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        const std::vector<Light>& lights,
        const DirectX::XMFLOAT3& cameraPos,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect,
        int& currBackBufferIndex,
        IDXGISwapChain* swapChain,
        UploadBuffer<CameraConstants>* cameraCB,
        UploadBuffer<ShadowConstants>* shadowCB,
        float nearZ,
        float farZ,
        ID3D12Resource* depthStencilBuffer = nullptr,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {},
        ID3D12DescriptorHeap* particleHeap = nullptr,
        D3D12_GPU_DESCRIPTOR_HANDLE particleSrv = {},
        D3D12_GPU_VIRTUAL_ADDRESS particleRenderCB = 0,
        UINT particleCount = 0);

    GBuffer* GetGBuffer() const { return mGBuffer.get(); }
    ID3D12PipelineState* GetShadowPSO() const { return mShadowPSO.Get(); }
    ID3D12RootSignature* GetShadowRootSignature() const { return mShadowRootSignature.Get(); }
    ID3D12RootSignature* GetParticleRootSignature() const { return mParticleRootSignature.Get(); }
    ID3D12PipelineState* GetParticleComputePSO() const { return mParticleComputePSO.Get(); }
    ID3D12PipelineState* GetParticlePSO() const { return mParticleGraphicsPSO.Get(); }
    ID3D12RootSignature* GetPostRootSignature() const { return mPostRootSignature.Get(); }
    ID3D12PipelineState* GetPostPSO() const { return mPostPSO.Get(); }

    // Обновить константы пост-эффектов (вызывать каждый кадр из Update).
    void UpdatePostConstants(float totalTime);
    void SetPostStrengths(float chromaticStrength, float vignetteStrength);

private:
    bool CreateGBuffer(UINT width, UINT height);
    bool CreateLightingResources();
    bool CreateShadowResources();
    bool CreateParticleResources();
    bool CreatePostResources();
    // Промежуточная сцена: lighting+частицы рисуются сюда,
    // пост-проход читает её как t0 вместе с G-Buffer.
    bool CreateSceneTexture();
    void DrawPostEffects();
    void DrawParticles(
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        ID3D12Resource* depthStencilBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        ID3D12DescriptorHeap* particleHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE particleSrv,
        D3D12_GPU_VIRTUAL_ADDRESS particleRenderCB,
        UINT particleCount,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect);

    ID3D12Device* mDevice;
    ID3D12CommandQueue* mCommandQueue;
    ID3D12GraphicsCommandList* mCommandList;
    ID3D12CommandAllocator* mCommandAllocator;
    ID3D12Fence* mFence;
    UINT mSwapChainBufferCount;
    DXGI_FORMAT mBackBufferFormat;
    UINT64 mFenceValue = 0;

    std::unique_ptr<GBuffer> mGBuffer;
    std::unique_ptr<UploadBuffer<LightConstants>> mLightingCB;
    ComPtr<ID3D12PipelineState> mLightingPSO;
    ComPtr<ID3D12RootSignature> mLightingRootSignature;
    ComPtr<ID3D12PipelineState> mShadowPSO;
    ComPtr<ID3D12RootSignature> mShadowRootSignature;
    ComPtr<ID3D12RootSignature> mParticleRootSignature;
    ComPtr<ID3D12PipelineState> mParticleComputePSO;
    ComPtr<ID3D12PipelineState> mParticleGraphicsPSO;

    // Пост-эффекты (как в референсе KG_Sem4_Laba7): fullscreen quad
    // без вершинного буфера, PS принимает сцену + G-Buffer.
    ComPtr<ID3D12RootSignature> mPostRootSignature;
    ComPtr<ID3D12PipelineState> mPostPSO;
    std::unique_ptr<UploadBuffer<PostConstants>> mPostCB;
    float mChromaticStrength = 0.25f;
    float mVignetteStrength = 0.78f;

    ComPtr<ID3D12Resource> mSceneTexture;
    ComPtr<ID3D12DescriptorHeap> mSceneRtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mSceneRtv = {};
    D3D12_RESOURCE_STATES mSceneState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ComPtr<ID3D12DescriptorHeap> mPostSrvHeap;
    UINT mWidth = 0;
    UINT mHeight = 0;
};
