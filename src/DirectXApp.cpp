#include "../h/DirectXApp.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <string>
#include "../h/Parser.h"
#include "../h/TgaLoader.h"
#include "../h/d3dUtil.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    bool IsCurtainMaterialToken(const std::string& materialName)
    {
        return materialName.find("curtain") != std::string::npos ||
               materialName.find("fabric") != std::string::npos;
    }

    D3D12_RESOURCE_BARRIER MakeTransition(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return barrier;
    }
}

struct CD3DX12_DEFAULT {};
extern const DECLSPEC_SELECTANY CD3DX12_DEFAULT D3D12_DEFAULT;

struct CD3DX12_RASTERIZER_DESC : public D3D12_RASTERIZER_DESC
{
    explicit CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT)
    {
        FillMode = D3D12_FILL_MODE_SOLID;
        CullMode = D3D12_CULL_MODE_BACK;
        FrontCounterClockwise = FALSE;
        DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        DepthClipEnable = TRUE;
        MultisampleEnable = FALSE;
        AntialiasedLineEnable = FALSE;
        ForcedSampleCount = 0;
        ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }
};

struct CD3DX12_BLEND_DESC : public D3D12_BLEND_DESC
{
    explicit CD3DX12_BLEND_DESC(CD3DX12_DEFAULT)
    {
        AlphaToCoverageEnable = FALSE;
        IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
        {
            FALSE, FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };
        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            RenderTarget[i] = defaultRenderTargetBlendDesc;
        }
    }
};

struct CD3DX12_DEPTH_STENCIL_DESC : public D3D12_DEPTH_STENCIL_DESC
{
    explicit CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT)
    {
        DepthEnable = TRUE;
        DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        StencilEnable = FALSE;
        StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
        { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
        FrontFace = defaultStencilOp;
        BackFace = defaultStencilOp;
    }
};

DirectXApp::DirectXApp(Window& window) : window(window)
{
    XMStoreFloat4x4(&mWorld, XMMatrixIdentity());
    XMStoreFloat4x4(&mView, XMMatrixIdentity());
    XMStoreFloat4x4(&mProj, XMMatrixIdentity());
}

DirectXApp::~DirectXApp()
{
    Shutdown();
}

bool DirectXApp::IsCurtainMaterialName(const std::string& materialName) const
{
    return IsCurtainMaterialToken(materialName);
}

UINT DirectXApp::GetMaterialSrvOffset() const
{
    return std::max<UINT>(1u, static_cast<UINT>(mSubmeshes.size()));
}

void DirectXApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(window.GetHwnd());
}

void DirectXApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void DirectXApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if (btnState & MK_RBUTTON)
    {
        const float sensitivity = 0.005f;
        float dx = (x - mLastMousePos.x) * sensitivity;
        float dy = (y - mLastMousePos.y) * sensitivity;
        mYaw += dx;
        mPitch += dy;
        mPitch = std::clamp(mPitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void DirectXApp::BuildInputLayout()
{
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void DirectXApp::BuildShaders()
{
    mvsByteCode = d3dUtil::CompileShader(L"../src/shaders.hlsl", nullptr, "VS", "vs_5_0");
    mpsByteCode = d3dUtil::CompileShader(L"../src/shaders.hlsl", nullptr, "PS", "ps_5_0");
}

void DirectXApp::BuildConstantBuffer()
{
    const UINT objectCount = std::max<UINT>(1u, static_cast<UINT>(mSubmeshes.size()));
    // Места под геометрию + по набору на каждый каскад теней (как в референсе).
    const UINT totalObjects = objectCount * (kNumCascades + 1u);
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device.Get(), totalObjects, true);

    const UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < objectCount; ++i)
    {
        ObjectConstants objConstants;
        XMStoreFloat4x4(&objConstants.mWorld, XMMatrixTranspose(XMMatrixIdentity()));
        XMStoreFloat4x4(&objConstants.mWorldViewProj, XMMatrixTranspose(XMMatrixIdentity()));
        objConstants.mUVTransform = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
        objConstants.mCurtainParams = XMFLOAT4(
            0.0f,
            (i < mSubmeshes.size() && IsCurtainMaterialName(mSubmeshes[i].MaterialName)) ? 0.08f : 0.0f,
            3.5f,
            1.8f);
        mObjectCB->CopyData(i, objConstants);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = mObjectCB->Resource()->GetGPUVirtualAddress() + i * objCBByteSize;
        cbvDesc.SizeInBytes = objCBByteSize;

        D3D12_CPU_DESCRIPTOR_HANDLE handle = cbvHandle;
        handle.ptr += i * mCbvSrvUavDescriptorSize;
        device->CreateConstantBufferView(&cbvDesc, handle);
    }
}

void DirectXApp::BuildRootSignature()
{
    D3D12_DESCRIPTOR_RANGE cbvRange = {};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &cbvRange;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParameters;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf()));

    ThrowIfFailed(device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

void DirectXApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    psoDesc.PS = {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };
    psoDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT;
    psoDesc.DSVFormat = mDepthStencilFormat;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void DirectXApp::BuildWireframePSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframeDesc = {};
    wireframeDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    wireframeDesc.PS = {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };
    wireframeDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
    wireframeDesc.pRootSignature = mRootSignature.Get();
    wireframeDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    wireframeDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    wireframeDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    wireframeDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    wireframeDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    wireframeDesc.SampleMask = UINT_MAX;
    wireframeDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    wireframeDesc.NumRenderTargets = 3;
    wireframeDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    wireframeDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    wireframeDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT;
    wireframeDesc.DSVFormat = mDepthStencilFormat;
    wireframeDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&wireframeDesc, IID_PPV_ARGS(&mWireframePSO)));
}

void DirectXApp::BuildObj(const std::string& path)
{
    mSubmeshes.clear();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    if (!LoadOBJ(path, vertices, indices, mSubmeshes))
    {
        MessageBoxA(nullptr, "Failed to load OBJ", "Error", MB_OK);
        return;
    }

    mIndexCount = static_cast<UINT>(indices.size());
    const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbByteSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mVertexBufferGPU)));

    void* mappedData = nullptr;
    mVertexBufferGPU->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices.data(), vbByteSize);
    mVertexBufferGPU->Unmap(0, nullptr);

    mVertexBufferView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
    mVertexBufferView.StrideInBytes = sizeof(Vertex);
    mVertexBufferView.SizeInBytes = vbByteSize;

    D3D12_RESOURCE_DESC ibDesc = {};
    ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ibDesc.Width = ibByteSize;
    ibDesc.Height = 1;
    ibDesc.DepthOrArraySize = 1;
    ibDesc.MipLevels = 1;
    ibDesc.SampleDesc.Count = 1;
    ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mIndexBufferGPU)));

    mIndexBufferGPU->Map(0, nullptr, &mappedData);
    memcpy(mappedData, indices.data(), ibByteSize);
    mIndexBufferGPU->Unmap(0, nullptr);

    mIndexBufferView.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mIndexBufferView.SizeInBytes = ibByteSize;
}

void DirectXApp::Shutdown()
{
    FlushCommandQueue();

    mParticleSimCB.reset();
    mParticleRenderCB.reset();
    mParticleCounterReadback.Reset();
    mParticleCounterResetUpload.Reset();
    for (auto& buf : mParticleBuffers)
    {
        buf.Reset();
    }
    for (auto& counter : mParticleCounters)
    {
        counter.Reset();
    }

    mShadowMap.Reset();
    mShadowDsvHeap.Reset();
    mShadowCB.reset();

    if (mRenderingSystem)
    {
        mRenderingSystem->Shutdown();
        mRenderingSystem.reset();
    }

    mCameraCB.reset();
    mObjectCB.reset();
    mPSO.Reset();
    mWireframePSO.Reset();
    mRootSignature.Reset();

    for (int i = 0; i < SwapChainBufferCount; ++i)
    {
        mSwapChainBuffer[i].Reset();
    }

    mDepthStencilBuffer.Reset();
    mRtvHeap.Reset();
    mDsvHeap.Reset();
    mCbvHeap.Reset();
    mSwapChain.Reset();
    mVertexBufferGPU.Reset();
    mVertexBufferUploader.Reset();
    mIndexBufferGPU.Reset();
    mIndexBufferUploader.Reset();
    mCommandList.Reset();
    mFence.Reset();
    mDirectCmdListAlloc.Reset();
    mCommandQueue.Reset();
    device.Reset();
    adapter.Reset();
    dxgiFactory.Reset();
}

bool DirectXApp::CreateDXGIFactory()
{
    return SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));
}

bool DirectXApp::GetHardwareAdapter()
{
    ComPtr<IDXGIFactory6> factory6;
    if (FAILED(dxgiFactory.As(&factory6)))
    {
        return false;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> currentAdapter;
        if (FAILED(factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&currentAdapter))))
        {
            break;
        }

        DXGI_ADAPTER_DESC1 desc;
        currentAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(currentAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
        {
            adapter = currentAdapter;
            return true;
        }
    }

    return false;
}

bool DirectXApp::CreateD3DDevice()
{
    if (!GetHardwareAdapter())
    {
        if (FAILED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
        {
            return false;
        }
    }

    return SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
}

bool DirectXApp::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mDirectCmdListAlloc)));
    ThrowIfFailed(device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&mCommandList)));
    mCommandList->Close();
    return true;
}

bool DirectXApp::CreateFence()
{
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
    mFenceValue = 0;
    return true;
}

void DirectXApp::FlushCommandQueue()
{
    if (!mCommandQueue || !mFence)
    {
        return;
    }

    ++mFenceValue;
    mCommandQueue->Signal(mFence.Get(), mFenceValue);
    if (mFence->GetCompletedValue() < mFenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(mFenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

bool DirectXApp::CreateSwapChain()
{
    RECT clientRect;
    GetClientRect(window.GetHandle(), &clientRect);
    mClientWidth = clientRect.right - clientRect.left;
    mClientHeight = clientRect.bottom - clientRect.top;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = window.GetHandle();
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    return SUCCEEDED(dxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, &mSwapChain));
}

void DirectXApp::QueryDescriptorSizes()
{
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool DirectXApp::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
    cbvHeapDesc.NumDescriptors = 1024;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
    return true;
}

bool DirectXApp::CreateRenderTargetViews()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < SwapChainBufferCount; ++i)
    {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
        device->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.ptr += mRtvDescriptorSize;
    }

    return true;
}

bool DirectXApp::CreateDepthStencilBuffer()
{
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = mDepthStencilFormat;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = mDepthStencilFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
    D3D12_RESOURCE_BARRIER barrier = MakeTransition(
        mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_DEPTH_READ);
    mCommandList->ResourceBarrier(1, &barrier);
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);
    FlushCommandQueue();

    return true;
}

void DirectXApp::CreateViewportAndScissor()
{
    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;
    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

void DirectXApp::SetViewportAndScissor()
{
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);
}

bool DirectXApp::Initialize()
{
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }
#endif

    if (!CreateDXGIFactory()) return false;
    if (!CreateD3DDevice()) return false;
    if (!CreateCommandObjects()) return false;
    if (!CreateFence()) return false;
    if (!CreateSwapChain()) return false;

    QueryDescriptorSizes();
    if (!CreateDescriptorHeaps()) return false;
    if (!CreateRenderTargetViews()) return false;
    if (!CreateDepthStencilBuffer()) return false;
    CreateViewportAndScissor();

    BuildInputLayout();
    BuildObj("../assets/sponza.obj");

    std::vector<ParsedMaterial> parsed;
    LoadMTL("../assets/sponza.mtl", parsed);
    const UINT materialSrvOffset = GetMaterialSrvOffset();

    UINT srvIndex = 0;
    for (auto& p : parsed)
    {
        Material mat;
        mat.Name = p.Name;
        mat.SrvHeapIndex = srvIndex++;

        if (!p.DiffuseMap.empty())
        {
            CreateTextureFromTGA("../assets/" + p.DiffuseMap, mat.DiffuseTexture);
        }
        else
        {
            CreateColorTexture(p.Kd, mat.DiffuseTexture);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE hDescriptor = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
        hDescriptor.ptr += (materialSrvOffset + mat.SrvHeapIndex) * mCbvSrvUavDescriptorSize;
        device->CreateShaderResourceView(mat.DiffuseTexture.Get(), &srvDesc, hDescriptor);
        mMaterials.push_back(mat);
    }

    BuildRootSignature();
    BuildShaders();
    BuildPSO();
    BuildWireframePSO();
    BuildConstantBuffer();

    mCameraCB = std::make_unique<UploadBuffer<CameraConstants>>(device.Get(), 1, true);

    mLights.clear();
    mLights.push_back(Light::CreateAmbientLight(XMFLOAT3(0.08f, 0.08f, 0.1f)));
    mLights.push_back(Light::CreateDirectionalLight(
        XMFLOAT3(0.5f, -1.0f, 0.25f),
        XMFLOAT3(1.0f, 0.95f, 0.9f),
        0.9f));
    mLights.push_back(Light::CreatePointLight(
        XMFLOAT3(5.0f, 3.0f, 0.0f),
        XMFLOAT3(0.25f, 1.0f, 0.35f),
        3.0f,
        7.0f));
    mLights.push_back(Light::CreatePointLight(
        XMFLOAT3(-4.0f, 2.0f, 2.5f),
        XMFLOAT3(1.0f, 0.85f, 0.2f),
        2.5f,
        8.0f));
    mLights.push_back(Light::CreateSpotLight(
        XMFLOAT3(0.0f, 5.0f, -2.0f),
        XMFLOAT3(0.0f, -1.0f, 0.2f),
        XMFLOAT3(0.35f, 0.45f, 1.0f),
        4.0f,
        15.0f,
        XM_PIDIV4));

    mRenderingSystem = std::make_unique<RenderingSystem>(
        device.Get(),
        mCommandQueue.Get(),
        mCommandList.Get(),
        mDirectCmdListAlloc.Get(),
        mFence.Get(),
        SwapChainBufferCount,
        mBackBufferFormat);

    if (!mRenderingSystem->Initialize(mClientWidth, mClientHeight))
    {
        return false;
    }

    CreateShadowResources();

    mParticleSimCB = std::make_unique<UploadBuffer<ParticleSimConstants>>(device.Get(), 1, true);
    mParticleRenderCB = std::make_unique<UploadBuffer<ParticleRenderConstants>>(device.Get(), 1, true);
    InitializeParticleResources();
    CreateParticleDescriptors();
    ResetParticles();

    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
        static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight), 1.0f, 200.0f);
    XMStoreFloat4x4(&mProj, P);

    mTimer.Reset();
    return true;
}

bool DirectXApp::InitializeApp()
{
    return Initialize();
}

ID3D12Resource* DirectXApp::CurrentBackBuffer() const
{
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::CurrentBackBufferView() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += mCurrBackBuffer * mRtvDescriptorSize;
    return handle;
}

void DirectXApp::OnResize()
{
}

void DirectXApp::OnKeyDown(WPARAM wParam)
{
    if (GetActiveWindow() != window.GetHwnd())
    {
        return;
    }

    if (wParam == 'T')
    {
        mAnimateTextures = !mAnimateTextures;
    }

    if (wParam == 'R')
    {
        mUVScaleU = 1.0f;
        mUVScaleV = 1.0f;
        mUVOffsetU = 0.0f;
        mUVOffsetV = 0.0f;
    }

    if (wParam == 'P')
    {
        mParticlesEnabled = !mParticlesEnabled;
        if (!mParticlesEnabled)
        {
            ResetParticles();
        }
    }
}

int DirectXApp::Run()
{
    MSG msg = { 0 };
    mTimer.Reset();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            mTimer.Tick();
            if (!mAppPaused)
            {
                CalculateFrameStats();
                Update(mTimer);
                Draw(mTimer);
            }
            else
            {
                Sleep(100);
            }
        }
    }

    return static_cast<int>(msg.wParam);
}

void DirectXApp::CalculateFrameStats()
{
    mFrameCount++;
    if ((mTimer.TotalTime() - mTimeElapsed) >= 1.0f)
    {
        float fps = static_cast<float>(mFrameCount);
        float mspf = 1000.0f / fps;

        std::wstring windowText = mMainWndCaption;
        windowText += L" FPS: " + std::to_wstring(fps);
        windowText += L" MSPF: " + std::to_wstring(mspf);
        windowText += L" | P: Particles ";
        windowText += (mParticlesEnabled ? L"ON (" : L"OFF (");
        windowText += std::to_wstring(mParticleAliveCount) + L")";
        SetWindowText(window.GetHandle(), windowText.c_str());

        mFrameCount = 0;
        mTimeElapsed += 1.0f;
    }
}

void DirectXApp::Update(const Timer& gt)
{
    const float dt = gt.DeltaTime();
    const float speed = 50.0f;

    XMFLOAT3 forward =
    {
        cosf(mPitch) * cosf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * sinf(mYaw)
    };

    XMVECTOR forwardVec = XMVector3Normalize(XMLoadFloat3(&forward));
    XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
    XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(worldUp, forwardVec));
    XMVECTOR upVec = XMVector3Normalize(XMVector3Cross(forwardVec, rightVec));

    XMVECTOR pos = XMLoadFloat3(&mEyePos);
    XMVECTOR delta = XMVectorZero();

    if (GetAsyncKeyState('W') & 0x8000) delta = XMVectorAdd(delta, XMVectorScale(forwardVec, speed * dt));
    if (GetAsyncKeyState('S') & 0x8000) delta = XMVectorSubtract(delta, XMVectorScale(forwardVec, speed * dt));
    if (GetAsyncKeyState('A') & 0x8000) delta = XMVectorSubtract(delta, XMVectorScale(rightVec, speed * dt));
    if (GetAsyncKeyState('D') & 0x8000) delta = XMVectorAdd(delta, XMVectorScale(rightVec, speed * dt));
    if (GetAsyncKeyState(VK_UP) & 0x8000) delta = XMVectorAdd(delta, XMVectorScale(worldUp, speed * dt));
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) delta = XMVectorSubtract(delta, XMVectorScale(worldUp, speed * dt));

    if (XMVectorGetX(XMVector3LengthSq(delta)) > 0.0f)
    {
        delta = XMVectorScale(XMVector3Normalize(delta), speed * dt);
    }

    pos += delta;
    XMStoreFloat3(&mEyePos, pos);

    XMMATRIX view = XMMatrixLookToLH(pos, forwardVec, upVec);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
        0.1f,
        200.0f);

    XMStoreFloat4x4(&mView, view);
    XMStoreFloat4x4(&mProj, proj);

    CameraConstants camConstants;
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    XMStoreFloat4x4(&camConstants.mInvViewProj, XMMatrixTranspose(invViewProj));
    camConstants.mCameraPos = mEyePos;
    camConstants.mScreenSize = XMFLOAT2(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
    mCameraCB->CopyData(0, camConstants);

    if (mAnimateTextures)
    {
        mUVOffsetU += dt * 0.1f;
        mUVOffsetV += dt * 0.05f;
        if (mUVOffsetU > 1.0f) mUVOffsetU -= 1.0f;
        if (mUVOffsetV > 1.0f) mUVOffsetV -= 1.0f;
    }

    if (GetAsyncKeyState('1') & 0x8000) mUVScaleU += dt * 2.0f;
    if (GetAsyncKeyState('2') & 0x8000) mUVScaleU -= dt * 2.0f;
    if (GetAsyncKeyState('3') & 0x8000) mUVScaleV += dt * 2.0f;
    if (GetAsyncKeyState('4') & 0x8000) mUVScaleV -= dt * 2.0f;

    mUVScaleU = (std::max)(0.1f, mUVScaleU);
    mUVScaleV = (std::max)(0.1f, mUVScaleV);

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX worldViewProj = world * view * proj;

    for (UINT i = 0; i < mSubmeshes.size(); ++i)
    {
        ObjectConstants objConstants;
        XMStoreFloat4x4(&objConstants.mWorld, XMMatrixTranspose(world));
        XMStoreFloat4x4(&objConstants.mWorldViewProj, XMMatrixTranspose(worldViewProj));
        objConstants.mUVTransform = XMFLOAT4(mUVScaleU, mUVScaleV, mUVOffsetU, mUVOffsetV);
        objConstants.mCurtainParams = XMFLOAT4(
            gt.TotalTime(),
            IsCurtainMaterialName(mSubmeshes[i].MaterialName) ? 0.08f : 0.0f,
            3.5f,
            1.8f);
        mObjectCB->CopyData(i, objConstants);
    }

    // Нелинейные сплиты каскадов + матрицы света для CSM.
    UpdateCascades();

    UpdateParticles(dt, gt.TotalTime());
}

void DirectXApp::Draw(const Timer& gt)
{
    if (mIndexCount == 0 || !mRenderingSystem)
    {
        return;
    }

    // Проход глубины по каскадам (CSM) перед геометрией.
    RenderShadowMaps();

    mRenderingSystem->GeometryPass(
        mPSO.Get(),
        mRootSignature.Get(),
        mCbvHeap.Get(),
        mCbvSrvUavDescriptorSize,
        GetMaterialSrvOffset(),
        mSubmeshes,
        mMaterials,
        mVertexBufferView,
        mIndexBufferView,
        mDepthStencilBuffer.Get(),
        DepthStencilView(),
        mScreenViewport,
        mScissorRect);

    // Симуляция частиц в Compute шейдере (отдельный список, свой flush).
    ExecuteParticleSimulation();

    D3D12_GPU_DESCRIPTOR_HANDLE particleSrv = {};
    D3D12_GPU_VIRTUAL_ADDRESS particleRenderCBAddr = 0;
    UINT particleDrawCount = 0;
    ID3D12DescriptorHeap* particleHeap = nullptr;
    if (ShouldDrawParticles() && mParticleRenderCB)
    {
        particleSrv = GetParticleDrawSrv();
        particleRenderCBAddr = mParticleRenderCB->Resource()->GetGPUVirtualAddress();
        particleDrawCount = mParticleAliveCount;
        particleHeap = mCbvHeap.Get();
    }

    mRenderingSystem->LightingPass(
        CurrentBackBuffer(),
        CurrentBackBufferView(),
        mLights,
        mEyePos,
        mScreenViewport,
        mScissorRect,
        mCurrBackBuffer,
        mSwapChain.Get(),
        mCameraCB.get(),
        mShadowCB.get(),
        0.1f,
        200.0f,
        mDepthStencilBuffer.Get(),
        DepthStencilView(),
        particleHeap,
        particleSrv,
        particleRenderCBAddr,
        particleDrawCount);

    FlushCommandQueue();
}

bool DirectXApp::GetDirectionalLightDir(DirectX::XMFLOAT3& outDir) const
{
    for (const auto& light : mLights)
    {
        if (light.Type == LIGHT_DIRECTIONAL)
        {
            outDir = light.Direction;
            return true;
        }
    }
    return false;
}

void DirectXApp::CreateShadowResources()
{
    mShadowMap.Reset();
    mShadowDsvHeap.Reset();
    mShadowCB.reset();

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kShadowMapSize;
    texDesc.Height = kShadowMapSize;
    texDesc.DepthOrArraySize = static_cast<UINT16>(kNumCascades);
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&mShadowMap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = kNumCascades;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (unsigned int i = 0; i < kNumCascades; ++i)
    {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        device->CreateDepthStencilView(mShadowMap.Get(), &dsvDesc, dsvHandle);
        dsvHandle.ptr += mDsvDescriptorSize;
    }

    mShadowViewport.TopLeftX = 0.0f;
    mShadowViewport.TopLeftY = 0.0f;
    mShadowViewport.Width = static_cast<float>(kShadowMapSize);
    mShadowViewport.Height = static_cast<float>(kShadowMapSize);
    mShadowViewport.MinDepth = 0.0f;
    mShadowViewport.MaxDepth = 1.0f;
    mShadowScissor = { 0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize) };
    mShadowMapStateIsSrv = false;

    mShadowCB = std::make_unique<UploadBuffer<ShadowConstants>>(device.Get(), 1, true);

    if (mRenderingSystem && mRenderingSystem->GetGBuffer())
    {
        mRenderingSystem->GetGBuffer()->CreateShadowSRV(device.Get(), mShadowMap.Get(), kNumCascades);
    }
}

void DirectXApp::UpdateCascades()
{
    if (!mShadowCB)
    {
        return;
    }

    const float nearZ = 0.1f;
    const float farZ = 200.0f;
    // Нелинейное распределение: lambda=1 чисто логарифмическое, 0 равномерное.
    const float lambda = 0.75f;
    const float clipRange = farZ - nearZ;

    float splits[kNumCascades] = {};
    for (unsigned int i = 0; i < kNumCascades; ++i)
    {
        const float p = static_cast<float>(i + 1) / static_cast<float>(kNumCascades);
        const float logSplit = nearZ * std::pow(farZ / nearZ, p);
        const float uniformSplit = nearZ + clipRange * p;
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }

    ShadowConstants shadow = {};
    shadow.CascadeSplits = XMFLOAT4(splits[0], splits[1], splits[2], 0.0f);

    XMFLOAT3 dirStorage = XMFLOAT3(0.5f, -1.0f, 0.25f);
    GetDirectionalLightDir(dirStorage);

    const XMMATRIX view = XMLoadFloat4x4(&mView);
    const float aspect = static_cast<float>(mClientWidth) / static_cast<float>((std::max)(1, mClientHeight));

    for (unsigned int i = 0; i < kNumCascades; ++i)
    {
        const float splitNear = (i == 0) ? nearZ : splits[i - 1];
        const float splitFar = splits[i];

        const XMMATRIX subProj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, splitNear, splitFar);

        // Углы усечённого фрустума каскада в мировых координатах.
        XMVECTOR frustumCorners[8] =
        {
            XMVectorSet(-1.0f, 1.0f, 0.0f, 1.0f),
            XMVectorSet(1.0f, 1.0f, 0.0f, 1.0f),
            XMVectorSet(-1.0f, -1.0f, 0.0f, 1.0f),
            XMVectorSet(1.0f, -1.0f, 0.0f, 1.0f),
            XMVectorSet(-1.0f, 1.0f, 1.0f, 1.0f),
            XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f),
            XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f),
            XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f),
        };

        const XMMATRIX invSub = XMMatrixInverse(nullptr, view * subProj);
        for (auto& corner : frustumCorners)
        {
            corner = XMVector4Transform(corner, invSub);
            corner = XMVectorDivide(corner, XMVectorSplatW(corner));
        }

        XMVECTOR center = XMVectorZero();
        for (int c = 0; c < 8; ++c)
        {
            center = XMVectorAdd(center, frustumCorners[c]);
        }
        center = XMVectorScale(center, 1.0f / 8.0f);

        float radius = 0.0f;
        for (int c = 0; c < 8; ++c)
        {
            const float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(frustumCorners[c], center)));
            radius = (std::max)(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&dirStorage));
        const XMVECTOR lightPos = XMVectorSubtract(center, XMVectorScale(lightDir, radius * 2.0f));
        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, center, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

        // Снаппинг к текселям для стабильности теней.
        const float texelWorldSize = (2.0f * radius) / static_cast<float>(kShadowMapSize);
        const XMVECTOR centerLS = XMVector3TransformCoord(center, lightView);
        const float offsetX = std::floor(XMVectorGetX(centerLS) / texelWorldSize) * texelWorldSize - XMVectorGetX(centerLS);
        const float offsetY = std::floor(XMVectorGetY(centerLS) / texelWorldSize) * texelWorldSize - XMVectorGetY(centerLS);
        lightView = lightView * XMMatrixTranslation(offsetX, offsetY, 0.0f);

        const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-radius, radius, -radius, radius, -radius * 4.0f, radius * 4.0f);

        XMStoreFloat4x4(&shadow.LightViewProj[i], XMMatrixTranspose(lightView * lightProj));
        mCascadeViewProj[i] = shadow.LightViewProj[i];
    }

    shadow.LightDirection = XMFLOAT4(dirStorage.x, dirStorage.y, dirStorage.z, 0.0f);
    mShadowCB->CopyData(0, shadow);
}

void DirectXApp::RenderShadowMaps()
{
    if (!mShadowMap || !mShadowDsvHeap || !mShadowCB || !mRenderingSystem)
    {
        return;
    }
    if (mSubmeshes.empty() || mIndexCount == 0)
    {
        return;
    }

    XMFLOAT3 dirStorage = XMFLOAT3(0.5f, -1.0f, 0.25f);
    GetDirectionalLightDir(dirStorage);
    (void)dirStorage;

    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    if (mShadowMapStateIsSrv)
    {
        D3D12_RESOURCE_BARRIER barrier = MakeTransition(
            mShadowMap.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        mCommandList->ResourceBarrier(1, &barrier);
        mShadowMapStateIsSrv = false;
    }

    mCommandList->RSSetViewports(1, &mShadowViewport);
    mCommandList->RSSetScissorRects(1, &mShadowScissor);
    mCommandList->SetPipelineState(mRenderingSystem->GetShadowPSO());
    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetShadowRootSignature());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    const UINT objectCount = std::max<UINT>(1u, static_cast<UINT>(mSubmeshes.size()));
    const UINT elementSize = mObjectCB->GetElementSize();
    const D3D12_GPU_VIRTUAL_ADDRESS baseAddr = mObjectCB->Resource()->GetGPUVirtualAddress();
    const XMMATRIX world = XMMatrixIdentity();

    for (unsigned int cascade = 0; cascade < kNumCascades; ++cascade)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsvHandle.ptr += cascade * mDsvDescriptorSize;

        mCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        mCommandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

        const XMMATRIX cascadeViewProj = XMMatrixTranspose(XMLoadFloat4x4(&mCascadeViewProj[cascade]));
        const UINT shadowBase = objectCount * (cascade + 1u);

        for (UINT i = 0; i < mSubmeshes.size(); ++i)
        {
            ObjectConstants obj;
            XMStoreFloat4x4(&obj.mWorld, XMMatrixTranspose(world));
            XMStoreFloat4x4(&obj.mWorldViewProj, XMMatrixTranspose(world * cascadeViewProj));
            obj.mUVTransform = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
            obj.mCurtainParams = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
            const UINT shadowIndex = shadowBase + i;
            mObjectCB->CopyData(shadowIndex, obj);

            mCommandList->SetGraphicsRootConstantBufferView(0, baseAddr + static_cast<UINT64>(shadowIndex) * elementSize);
            mCommandList->DrawIndexedInstanced(mSubmeshes[i].IndexCount, 1, mSubmeshes[i].IndexStart, 0, 0);
        }
    }

    D3D12_RESOURCE_BARRIER toSrv = MakeTransition(
        mShadowMap.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &toSrv);
    mShadowMapStateIsSrv = true;

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();
}

void DirectXApp::CreateTextureFromTGA(
    const std::string& path,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture)
{
    TgaImage image;
    if (!LoadTGA(path, image))
    {
        throw std::runtime_error("Failed to load TGA: " + path);
    }

    UINT pixelSize = static_cast<UINT>(image.data.size() / (image.width * image.height));
    if (pixelSize == 3)
    {
        std::vector<uint8_t> converted(image.width * image.height * 4);
        for (UINT i = 0; i < image.width * image.height; ++i)
        {
            converted[i * 4 + 0] = image.data[i * 3 + 0];
            converted[i * 4 + 1] = image.data[i * 3 + 1];
            converted[i * 4 + 2] = image.data[i * 3 + 2];
            converted[i * 4 + 3] = 255;
        }
        image.data = std::move(converted);
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = image.width;
    texDesc.Height = image.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12Resource> uploadBuffer;
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);
    BYTE* dest = reinterpret_cast<BYTE*>(mapped);
    BYTE* srcData = image.data.data();
    UINT rowPitch = (image.width * 4 + 255) & ~255;
    for (UINT y = 0; y < image.height; ++y)
    {
        memcpy(dest + y * rowPitch, srcData + y * image.width * 4, image.width * 4);
    }
    uploadBuffer->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &src.PlacedFootprint, nullptr, nullptr, nullptr);

    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = MakeTransition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();
}

void DirectXApp::InitializeParticleResources()
{
    static_assert(sizeof(ParticleGpu) == 64, "ParticleGpu must be 64 bytes (HLSL layout).");

    const UINT64 particleBufferBytes = static_cast<UINT64>(kMaxParticles) * sizeof(ParticleGpu);
    constexpr UINT64 counterBytes = 4096;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_RESOURCE_DESC particleDesc = {};
        particleDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        particleDesc.Width = particleBufferBytes;
        particleDesc.Height = 1;
        particleDesc.DepthOrArraySize = 1;
        particleDesc.MipLevels = 1;
        particleDesc.Format = DXGI_FORMAT_UNKNOWN;
        particleDesc.SampleDesc.Count = 1;
        particleDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        particleDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &particleDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&mParticleBuffers[i])));

        D3D12_RESOURCE_DESC counterDesc = {};
        counterDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        counterDesc.Width = counterBytes;
        counterDesc.Height = 1;
        counterDesc.DepthOrArraySize = 1;
        counterDesc.MipLevels = 1;
        counterDesc.Format = DXGI_FORMAT_UNKNOWN;
        counterDesc.SampleDesc.Count = 1;
        counterDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        counterDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&mParticleCounters[i])));
    }

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resetDesc = {};
    resetDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resetDesc.Width = counterBytes;
    resetDesc.Height = 1;
    resetDesc.DepthOrArraySize = 1;
    resetDesc.MipLevels = 1;
    resetDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resetDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &resetDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mParticleCounterResetUpload)));

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(mParticleCounterResetUpload->Map(0, &readRange, &mapped));
    memset(mapped, 0, static_cast<size_t>(counterBytes));
    D3D12_RANGE writeRange = { 0, sizeof(UINT) };
    mParticleCounterResetUpload->Unmap(0, &writeRange);

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    ThrowIfFailed(device->CreateCommittedResource(
        &readbackHeap,
        D3D12_HEAP_FLAG_NONE,
        &resetDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mParticleCounterReadback)));

    mParticleSourceIndexA = true;
    mParticleAliveCount = 0;
    mParticleSpawnCount = 0;
    mParticleSpawnAccumulator = 0.0f;
    mParticleBufferStates = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
    mParticleCounterStates = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
}

void DirectXApp::CreateParticleDescriptors()
{
    if (!mCbvHeap || !mParticleBuffers[0] || !mParticleBuffers[1] ||
        !mParticleCounters[0] || !mParticleCounters[1])
    {
        return;
    }

    const UINT base = GetMaterialSrvOffset() + static_cast<UINT>(mMaterials.size());
    if (base + 4 > 1024)
    {
        return;
    }

    mParticleSrvIndex = base;
    mParticleUavStartIndex = base + 2;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(mParticleSrvIndex) * mCbvSrvUavDescriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = kMaxParticles;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleGpu);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    for (UINT i = 0; i < 2; ++i)
    {
        device->CreateShaderResourceView(mParticleBuffers[i].Get(), &srvDesc, cpuHandle);
        cpuHandle.ptr += mCbvSrvUavDescriptorSize;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = kMaxParticles;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleGpu);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    for (UINT i = 0; i < 2; ++i)
    {
        device->CreateUnorderedAccessView(
            mParticleBuffers[i].Get(),
            mParticleCounters[i].Get(),
            &uavDesc,
            cpuHandle);
        cpuHandle.ptr += mCbvSrvUavDescriptorSize;
    }
}

void DirectXApp::ResetParticles()
{
    mParticleSourceIndexA = true;
    mParticleAliveCount = 0;
    mParticleSpawnCount = 0;
    mParticleSpawnAccumulator = 0.0f;
}

void DirectXApp::UpdateParticleCounterFromReadback()
{
    if (!mParticleCounterReadback)
    {
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, sizeof(UINT) };
    if (SUCCEEDED(mParticleCounterReadback->Map(0, &readRange, &mapped)) && mapped)
    {
        const UINT count = *reinterpret_cast<const UINT*>(mapped);
        mParticleAliveCount = (std::min)(count, static_cast<UINT>(kMaxParticles));
        D3D12_RANGE writeRange = { 0, 0 };
        mParticleCounterReadback->Unmap(0, &writeRange);
    }
}

void DirectXApp::UpdateParticles(float dt, float totalTime)
{
    mParticleSpawnCount = 0;
    if (!mParticleSimCB || !mParticleRenderCB || !mParticlesEnabled)
    {
        return;
    }

    UpdateParticleCounterFromReadback();

    constexpr float kSpawnRate = 260.0f;
    const float simDt = (std::min)(dt, 0.1f);
    mParticleSpawnAccumulator += simDt * kSpawnRate;
    mParticleSpawnCount = static_cast<UINT>(mParticleSpawnAccumulator);
    mParticleSpawnAccumulator -= static_cast<float>(mParticleSpawnCount);

    if (mParticleAliveCount >= kMaxParticles)
    {
        mParticleSpawnCount = 0;
    }
    else
    {
        const UINT freeCount = kMaxParticles - mParticleAliveCount;
        mParticleSpawnCount = (std::min)(mParticleSpawnCount, freeCount);
    }

    ParticleSimConstants sim = {};
    sim.Dt = simDt;
    sim.TotalTime = totalTime;
    sim.AliveCount = mParticleAliveCount;
    sim.SpawnCount = mParticleSpawnCount;
    sim.EmitterPos = XMFLOAT3(0.0f, 5.0f, 0.0f);
    sim.BaseSize = 0.18f;
    sim.EmitterVelocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
    sim.Gravity = 9.8f;
    sim.LifeMin = 2.0f;
    sim.LifeMax = 4.0f;
    sim.SpeedMin = 2.0f;
    sim.SpeedMax = 5.0f;
    sim.MaxParticles = kMaxParticles;
    sim.CollisionCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    sim.CollisionRadius = 0.0f;
    sim.Restitution = 0.5f;
    mParticleSimCB->CopyData(0, sim);

    // Базис камеры для билбордов + ViewProj для геометрического шейдера.
    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        cosf(mPitch) * cosf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * sinf(mYaw),
        0.0f));
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));

    ParticleRenderConstants render = {};
    const XMMATRIX view = XMLoadFloat4x4(&mView);
    const XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMStoreFloat4x4(&render.ViewProj, XMMatrixTranspose(view * proj));
    XMStoreFloat3(&render.CameraRight, right);
    XMStoreFloat3(&render.CameraUp, up);
    render.RenderSizeScale = 1.0f;
    render.AlphaDiscard = 0.5f;
    mParticleRenderCB->CopyData(0, render);
}

void DirectXApp::TransitionParticleResource(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES& currentState,
    D3D12_RESOURCE_STATES targetState)
{
    if (!cmdList || !resource || currentState == targetState)
    {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = MakeTransition(resource, currentState, targetState);
    cmdList->ResourceBarrier(1, &barrier);
    currentState = targetState;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXApp::GetParticleDrawSrv() const
{
    const UINT sourceIndex = mParticleSourceIndexA ? 0u : 1u;
    D3D12_GPU_DESCRIPTOR_HANDLE handle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(mParticleSrvIndex + sourceIndex) * mCbvSrvUavDescriptorSize;
    return handle;
}

void DirectXApp::ExecuteParticleSimulation()
{
    if (!mParticleSimCB || !mParticleBuffers[0] || !mParticleBuffers[1] ||
        !mParticleCounters[0] || !mParticleCounters[1] || !mParticleCounterReadback ||
        !mParticleCounterResetUpload || !mRenderingSystem ||
        !mRenderingSystem->GetParticleComputePSO() || !mRenderingSystem->GetParticleRootSignature())
    {
        return;
    }

    const UINT sourceIndex = mParticleSourceIndexA ? 0u : 1u;
    const UINT destIndex = 1u - sourceIndex;

    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(
        mDirectCmdListAlloc.Get(),
        mRenderingSystem->GetParticleComputePSO()));

    TransitionParticleResource(
        mCommandList.Get(),
        mParticleCounters[destIndex].Get(),
        mParticleCounterStates[destIndex],
        D3D12_RESOURCE_STATE_COPY_DEST);
    mCommandList->CopyBufferRegion(
        mParticleCounters[destIndex].Get(),
        0,
        mParticleCounterResetUpload.Get(),
        0,
        sizeof(UINT));

    const UINT workItemCount = (std::max)(mParticleAliveCount, mParticleSpawnCount);
    if (workItemCount == 0u)
    {
        TransitionParticleResource(
            mCommandList.Get(),
            mParticleCounters[destIndex].Get(),
            mParticleCounterStates[destIndex],
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        mCommandList->CopyBufferRegion(
            mParticleCounterReadback.Get(), 0,
            mParticleCounters[destIndex].Get(), 0,
            sizeof(UINT));
        mParticleSourceIndexA = (destIndex == 0u);

        ThrowIfFailed(mCommandList->Close());
        ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
        mCommandQueue->ExecuteCommandLists(1, cmdLists);
        FlushCommandQueue();
        return;
    }

    TransitionParticleResource(
        mCommandList.Get(),
        mParticleBuffers[sourceIndex].Get(),
        mParticleBufferStates[sourceIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionParticleResource(
        mCommandList.Get(),
        mParticleBuffers[destIndex].Get(),
        mParticleBufferStates[destIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionParticleResource(
        mCommandList.Get(),
        mParticleCounters[sourceIndex].Get(),
        mParticleCounterStates[sourceIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionParticleResource(
        mCommandList.Get(),
        mParticleCounters[destIndex].Get(),
        mParticleCounterStates[destIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    mCommandList->SetComputeRootSignature(mRenderingSystem->GetParticleRootSignature());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = gpuBase;
    srvHandle.ptr += static_cast<SIZE_T>(mParticleSrvIndex + sourceIndex) * mCbvSrvUavDescriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE consumeHandle = gpuBase;
    consumeHandle.ptr += static_cast<SIZE_T>(mParticleUavStartIndex + sourceIndex) * mCbvSrvUavDescriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE appendHandle = gpuBase;
    appendHandle.ptr += static_cast<SIZE_T>(mParticleUavStartIndex + destIndex) * mCbvSrvUavDescriptorSize;

    ID3D12DescriptorHeap* heaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
    mCommandList->SetComputeRootDescriptorTable(1, consumeHandle);
    mCommandList->SetComputeRootDescriptorTable(2, appendHandle);
    mCommandList->SetComputeRootConstantBufferView(3, mParticleSimCB->Resource()->GetGPUVirtualAddress());

    const UINT groupsX = (workItemCount + 255u) / 256u;
    mCommandList->Dispatch(groupsX, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = mParticleBuffers[destIndex].Get();
    mCommandList->ResourceBarrier(1, &uavBarrier);

    TransitionParticleResource(
        mCommandList.Get(),
        mParticleCounters[destIndex].Get(),
        mParticleCounterStates[destIndex],
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    mCommandList->CopyBufferRegion(
        mParticleCounterReadback.Get(), 0,
        mParticleCounters[destIndex].Get(), 0,
        sizeof(UINT));

    // Готовый буфер — в SRV, чтобы проход освещения/частиц рисовал без лишних барьеров.
    TransitionParticleResource(
        mCommandList.Get(),
        mParticleBuffers[destIndex].Get(),
        mParticleBufferStates[destIndex],
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    mParticleSourceIndexA = (destIndex == 0u);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();
}

void DirectXApp::CreateColorTexture(
    const DirectX::XMFLOAT3& color,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture)
{
    UINT r = static_cast<UINT>(color.x * 255.0f);
    UINT g = static_cast<UINT>(color.y * 255.0f);
    UINT b = static_cast<UINT>(color.z * 255.0f);
    UINT pixel = (255 << 24) | (b << 16) | (g << 8) | r;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12Resource> uploadBuffer;
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);
    memcpy(mapped, &pixel, sizeof(UINT));
    uploadBuffer->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &src.PlacedFootprint, nullptr, nullptr, nullptr);

    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = MakeTransition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();
}
