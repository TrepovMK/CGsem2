#pragma once

#include "Light.h"
#include "UploadBuffer.h"
#include "mesh_data.h"
#include "RenderingSystem.h"

#include <array>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <random>

#include "../h/ObjectConstants.h"
#include "../h/CameraConstants.h"
#include "../h/Timer.h"
#include "../h/Vertex.h"
#include "../h/Material.h"
#include "../h/Submesh.h"
#include "../h/ThrowIfFailed.h"
#include "../h/Window.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

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
    void BuildScene();
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
    void LoadModels();
    void LoadSponzaModel();
    void BuildGeometryBuffers();
    void LoadTextures();
    void CreateFallbackTextures();
    void BindSubmeshTextures();
    void BuildConstantBuffers();
    void BuildMainSrvHeap();
    void BuildLights();
    void UpdateCamera(float dt);
    void FlushCommandQueue();

    void ActivateScene(int index, bool resetCamera);
    void BuildScenePresets();
    void RebuildSceneObjectTransforms();
    void BuildOctree();
    void InsertObjectIntoOctree(unsigned int objectIndex, int nodeIndex, int depth);
    void CollectVisibleObjects();
    void CollectVisibleFromOctree(int nodeIndex, const BoundingFrustum& frustum);
    void UpdateWindowTitle();

    struct DebugLineVertex {
        XMFLOAT3 pos = {0, 0, 0};
        XMFLOAT3 color = {1, 1, 1};
    };
    void BuildDebugLines();
    void DrawMinimap();
    XMMATRIX GetMinimapViewProj(float miniAspect) const;
    void AppendBoxLines(const BoundingBox& box, const XMFLOAT3& color);
    void AppendOctreeNodeLines(int nodeIndex, int depth);
    void EnsureDebugBuffer(size_t neededVerts);

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const { return mDsvHeap->GetCPUDescriptorHandleForHeapStart(); }
    ID3D12Resource* CurrentBackBuffer() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(unsigned int heapIndex) const;

    bool CreateDXGIFactory();
    bool GetHardwareAdapter();
    bool CreateD3DDevice();
    bool CreateCommandObjects();
    bool CreateFence();
    bool CreateSwapChain();
    void QueryDescriptorSizes();
    bool CreateDescriptorHeaps();
    bool CreateRenderTargetViews();
    bool CreateDepthStencilBuffer();
    void CreateViewportAndScissor();
    void SetViewportAndScissor();

    struct TextureResource {
        std::wstring path;
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> uploadHeap;
        unsigned int srvHeapIndex = 0;
    };

    struct ModelAsset {
        std::string name;
        unsigned int startIndex = 0;
        unsigned int submeshCount = 0;
        XMFLOAT3 localCenter = {0, 0, 0};
        XMFLOAT3 localExtents = {0, 0, 0};
        float localRadius = 0;
    };

    struct SceneObject {
        int modelIndex = -1;
        XMFLOAT3 position = {0, 0, 0};
        float scale = 1.0f;
        float rotationY = 0.0f;
        XMFLOAT4X4 world = {};
        BoundingBox worldBounds;
    };

    struct ScenePreset {
        std::wstring name;
        std::vector<SceneObject> objects;
        XMFLOAT3 cameraPos = {0, 0, 0};
        float cameraYaw = 0.0f;
        float cameraPitch = 0.0f;
    };

    struct OctreeNode {
        XMFLOAT3 center = {0, 0, 0};
        XMFLOAT3 extents = {0, 0, 0};
        BoundingBox bounds;
        std::vector<unsigned int> objectIndices;
        int children[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        bool subdivided = false;
    };

    static constexpr unsigned int SwapChainBufferCount = 2;
    static constexpr unsigned int LightingCbElementCount = 2048;
    static constexpr int kMaxOctreeDepth = 8;
    static constexpr int kMaxLeafObjects = 28;

    DirectXApp* dxApp = nullptr;
    Window& window;

    std::unique_ptr<RenderingSystem> mRenderingSystem;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
    std::unique_ptr<UploadBuffer<PassConstants>> mPassCB;
    std::unique_ptr<UploadBuffer<LightingConstants>> mLightingCB;

    MeshData mSceneMesh;
    ComPtr<ID3D12Resource> mVertexBufferGPU;
    ComPtr<ID3D12Resource> mVertexBufferUploader;
    ComPtr<ID3D12Resource> mIndexBufferGPU;
    ComPtr<ID3D12Resource> mIndexBufferUploader;

    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView = {};

    std::vector<TextureResource> mTextureResources;
    std::unordered_map<std::string, unsigned int> mTextureNameToIndex;

    unsigned int mFallbackDiffuseIndex = 0;
    unsigned int mFallbackNormalIndex = 0;
    unsigned int mFallbackDisplacementIndex = 0;

    unsigned int mTextureSrvStart = 3;
    unsigned int mGBufferSrvStart = 0;

    std::vector<LightData> mLights;

    std::vector<ModelAsset> mModelAssets;

    std::vector<ScenePreset> mScenePresets;
    std::vector<SceneObject> mSceneObjects;
    int mActiveSceneIndex = 0;

    bool mFrustumCullingEnabled = false;
    bool mOctreeCullingEnabled = false;
    std::vector<unsigned int> mVisibleObjects;

    std::vector<OctreeNode> mOctreeNodes;

    // Minimap (top-down debug view): frustum outline + object boxes + octree.
    bool mMinimapEnabled = true;
    bool mVWasDown = false;
    BoundingFrustum mLastWorldFrustum;
    bool mHasLastFrustum = false;
    std::vector<DebugLineVertex> mDebugLines;
    ComPtr<ID3D12Resource> mDebugVB;
    DebugLineVertex* mDebugVBMapped = nullptr;
    size_t mDebugVBCapacity = 0;
    D3D12_VERTEX_BUFFER_VIEW mDebugVBView = {};

    int mObjectsTestedThisFrame = 0;
    int mOctreeNodesVisitedThisFrame = 0;

    XMFLOAT3 mEyePos = {0.0f, 2.0f, -12.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    POINT mLastMousePos = {0, 0};

    int mDebugViewMode = 1;
    bool mF1WasDown = false;
    bool mF2WasDown = false;
    bool mF3WasDown = false;
    bool mTWasDown = false;
    bool mRWasDown = false;
    bool mCWasDown = false;
    bool mOWasDown = false;
    bool mDigit1WasDown = false;
    bool mDigit2WasDown = false;
    bool mDigit3WasDown = false;
    bool mAnimateTextures = false;
    bool mTitleDirty = true;
    float mTexAnimU = 0.0f;
    float mTexAnimV = 0.0f;
    float mTexScaleU = 1.0f;
    float mTexScaleV = 1.0f;
    int mLastTitleMode = -1;

    ComPtr<IDXGIFactory4> dxgiFactory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;
    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;

    ComPtr<IDXGISwapChain> mSwapChain;
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

    int mClientWidth = 1280;
    int mClientHeight = 720;
    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    Timer mTimer;
    bool mAppPaused = false;
    bool mResizing = false;
    int mFrameCount = 0;
    float mTimeElapsed = 0.0f;
    std::wstring mMainWndCaption = L"DirectX 12 Tessellation";

    ID3D12Resource* CurrentBackBufferResource() const { return mSwapChainBuffer[mCurrBackBuffer].Get(); }
};
