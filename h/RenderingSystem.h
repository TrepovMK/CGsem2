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
        float nearZ,
        float farZ);

    GBuffer* GetGBuffer() const { return mGBuffer.get(); }

private:
    bool CreateGBuffer(UINT width, UINT height);
    bool CreateLightingResources();
    bool CreateDebugResources();
    void RenderDebugOverlays(const D3D12_VIEWPORT& fullViewport, float nearZ, float farZ);

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
    ComPtr<ID3D12PipelineState> mDebugPSO;
    ComPtr<ID3D12RootSignature> mDebugRootSignature;
};
