#pragma once

#include <DirectXMath.h>
#include <array>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include "../h/CameraConstants.h"
#include "../h/ObjectConstants.h"
#include "../h/Timer.h"
#include "../h/UploadBuffer.h"
#include "../h/vertex.h"
#include "Light.h"
#include "Material.h"
#include "MathHelper.h"
#include "RenderingSystem.h"
#include "Submesh.h"
#include "ThrowIfFailed.h"
#include "Window.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DirectXApp
{
public:
    DirectXApp(Window& window);
    ~DirectXApp();

    bool Initialize();
    void Shutdown();
    int Run();
    virtual bool InitializeApp();
    virtual void Update(const Timer& gt);
    virtual void Draw(const Timer& gt);
    void BuildObj(const std::string& path);
    virtual void CalculateFrameStats();
    void StopTimer() { mTimer.Stop(); }
    void StartTimer() { mTimer.Start(); }
    bool IsPaused() const { return mAppPaused; }
    Timer& GetTimer() { return mTimer; }
    virtual void OnMouseDown(WPARAM btnState, int x, int y);
    virtual void OnMouseUp(WPARAM btnState, int x, int y);
    virtual void OnMouseMove(WPARAM btnState, int x, int y);
    virtual void OnResize();
    virtual void OnKeyDown(WPARAM wParam);
    void SetDirectXApp(DirectXApp* app) { dxApp = app; }
    DirectXApp* GetDirectXApp() const { return dxApp; }

private:
    std::vector<Light> mLights;
    std::unique_ptr<RenderingSystem> mRenderingSystem;
    std::unique_ptr<UploadBuffer<CameraConstants>> mCameraCB;

    float mYaw = 0.0f;
    float mPitch = 0.0f;
    float mUVOffsetU = 0.0f;
    float mUVOffsetV = 0.0f;
    float mUVScaleU = 1.0f;
    float mUVScaleV = 1.0f;
    bool mAnimateTextures = false;

    std::vector<Submesh> mSubmeshes;
    std::vector<Material> mMaterials;
    void CreateTextureFromTGA(const std::string& path, Microsoft::WRL::ComPtr<ID3D12Resource>& texture);
    void CreateColorTexture(const DirectX::XMFLOAT3& color, Microsoft::WRL::ComPtr<ID3D12Resource>& texture);
    bool IsCurtainMaterialName(const std::string& materialName) const;
    UINT GetMaterialSrvOffset() const;

    // Каскадные тени (CSM): pecypcы, нелинейные сплиты, рендер глубины.
    void CreateShadowResources();
    void UpdateCascades();
    void RenderShadowMaps();
    bool GetDirectionalLightDir(DirectX::XMFLOAT3& outDir) const;

    DirectXApp* dxApp = nullptr;
    XMFLOAT3 mEyePos = XMFLOAT3(0.0f, 0.0f, 0.0f);
    Window& window;

    ComPtr<IDXGIFactory4> dxgiFactory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;
    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;

    ComPtr<IDXGISwapChain> mSwapChain;
    static const int SwapChainBufferCount = 2;
    ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
    int mCurrBackBuffer = 0;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap;
    ComPtr<ID3D12Resource> mDepthStencilBuffer;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    int mClientWidth = 800;
    int mClientHeight = 600;
    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    Timer mTimer;
    bool mAppPaused = false;
    bool mResizing = false;
    int mFrameCount = 0;
    float mTimeElapsed = 0.0f;
    std::wstring mMainWndCaption = L"DirectX 12 Framework";

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    ComPtr<ID3D12Resource> mVertexBufferGPU;
    ComPtr<ID3D12Resource> mVertexBufferUploader;
    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView;
    ComPtr<ID3D12Resource> mIndexBufferGPU;
    ComPtr<ID3D12Resource> mIndexBufferUploader;
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView;

    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPSO;
    ComPtr<ID3D12PipelineState> mWireframePSO;
    bool mWireframeMode = false;

    // Ресурсы каскадных теней.
    ComPtr<ID3D12Resource> mShadowMap;
    ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;
    std::unique_ptr<UploadBuffer<ShadowConstants>> mShadowCB;
    XMFLOAT4X4 mCascadeViewProj[kNumCascades] = {};
    D3D12_VIEWPORT mShadowViewport = {};
    D3D12_RECT mShadowScissor = {};
    bool mShadowMapStateIsSrv = false;

    // Система частиц: два StructuredBuffer (Append/Consume), обновление в Compute,
    // развёртка точек в билборды в геометрическом шейдере.
    struct ParticleGpu
    {
        XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
        float Size = 0.16f;
        XMFLOAT3 Velocity = { 0.0f, 0.0f, 0.0f };
        float Age = 0.0f;
        XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Lifetime = 1.0f;
        XMFLOAT3 Padding = { 0.0f, 0.0f, 0.0f };
    };

    struct ParticleSimConstants
    {
        float Dt = 0.0f;
        float TotalTime = 0.0f;
        UINT AliveCount = 0;
        UINT SpawnCount = 0;
        XMFLOAT3 EmitterPos = { 0.0f, 0.0f, 0.0f };
        float BaseSize = 0.16f;
        XMFLOAT3 EmitterVelocity = { 0.0f, 0.0f, 0.0f };
        float Gravity = 9.8f;
        float LifeMin = 1.0f;
        float LifeMax = 3.0f;
        float SpeedMin = 5.0f;
        float SpeedMax = 12.0f;
        UINT MaxParticles = 2048;
        XMFLOAT3 Pad0 = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 CollisionCenter = { 0.0f, 0.0f, 0.0f };
        float CollisionRadius = 0.0f;
        float Restitution = 0.5f;
        XMFLOAT3 Pad1 = { 0.0f, 0.0f, 0.0f };
    };

    struct ParticleRenderConstants
    {
        XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
        XMFLOAT3 CameraRight = { 1.0f, 0.0f, 0.0f };
        float RenderSizeScale = 1.0f;
        XMFLOAT3 CameraUp = { 0.0f, 1.0f, 0.0f };
        float AlphaDiscard = 0.5f;
    };

    void InitializeParticleResources();
    void CreateParticleDescriptors();
    void ResetParticles();
    void UpdateParticles(float dt, float totalTime);
    void ExecuteParticleSimulation();
    void UpdateParticleCounterFromReadback();
    void TransitionParticleResource(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& currentState,
        D3D12_RESOURCE_STATES targetState);
    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleDrawSrv() const;
    bool ShouldDrawParticles() const { return mParticlesEnabled && mParticleAliveCount > 0; }

    static constexpr UINT kMaxParticles = 2048;
    std::array<ComPtr<ID3D12Resource>, 2> mParticleBuffers = {};
    std::array<ComPtr<ID3D12Resource>, 2> mParticleCounters = {};
    std::array<D3D12_RESOURCE_STATES, 2> mParticleBufferStates = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };
    std::array<D3D12_RESOURCE_STATES, 2> mParticleCounterStates = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };
    ComPtr<ID3D12Resource> mParticleCounterReadback;
    ComPtr<ID3D12Resource> mParticleCounterResetUpload;
    std::unique_ptr<UploadBuffer<ParticleSimConstants>> mParticleSimCB;
    std::unique_ptr<UploadBuffer<ParticleRenderConstants>> mParticleRenderCB;
    UINT mParticleSrvIndex = 0;
    UINT mParticleUavStartIndex = 0;
    bool mParticleSourceIndexA = true;
    UINT mParticleAliveCount = 0;
    UINT mParticleSpawnCount = 0;
    float mParticleSpawnAccumulator = 0.0f;
    bool mParticlesEnabled = true;

    POINT mLastMousePos;
    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();
    UINT mIndexCount = 0;

    bool CreateDXGIFactory();
    bool GetHardwareAdapter();
    bool CreateD3DDevice();
    bool CreateCommandObjects();
    bool CreateFence();
    void FlushCommandQueue();
    bool CreateSwapChain();
    void QueryDescriptorSizes();
    bool CreateDescriptorHeaps();
    bool CreateRenderTargetViews();
    bool CreateDepthStencilBuffer();
    void CreateViewportAndScissor();
    void SetViewportAndScissor();
    void BuildInputLayout();
    void BuildShaders();
    void BuildConstantBuffer();
    void BuildRootSignature();
    void BuildPSO();
    void BuildWireframePSO();
    ID3D12Resource* CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const { return mDsvHeap->GetCPUDescriptorHandleForHeapStart(); }
};
