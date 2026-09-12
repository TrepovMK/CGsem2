#include "../h/RenderingSystem.h"
#include "../h/ThrowIfFailed.h"
#include "../h/d3dUtil.h"

namespace
{
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

RenderingSystem::RenderingSystem(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12Fence* fence,
    UINT swapChainBufferCount,
    DXGI_FORMAT backBufferFormat)
    : mDevice(device)
    , mCommandQueue(commandQueue)
    , mCommandList(commandList)
    , mCommandAllocator(commandAllocator)
    , mFence(fence)
    , mSwapChainBufferCount(swapChainBufferCount)
    , mBackBufferFormat(backBufferFormat)
{
}

RenderingSystem::~RenderingSystem()
{
    Shutdown();
}

bool RenderingSystem::Initialize(UINT width, UINT height)
{
    return CreateGBuffer(width, height) && CreateLightingResources() && CreateDebugResources();
}

bool RenderingSystem::CreateGBuffer(UINT width, UINT height)
{
    mGBuffer = std::make_unique<GBuffer>();
    return mGBuffer->Initialize(mDevice, width, height);
}

bool RenderingSystem::CreateLightingResources()
{
    auto vsLighting = d3dUtil::CompileShader(L"../src/lighting.hlsl", nullptr, "VS", "vs_5_0");
    auto psLighting = d3dUtil::CompileShader(L"../src/lighting.hlsl", nullptr, "PS", "ps_5_0");
    if (!vsLighting || !psLighting)
    {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE gbufferRanges[3] = {};
    for (UINT i = 0; i < 3; ++i)
    {
        gbufferRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRanges[i].NumDescriptors = 1;
        gbufferRanges[i].BaseShaderRegister = i;
        gbufferRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
    rootParams[0].DescriptorTable.pDescriptorRanges = gbufferRanges;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
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

    ThrowIfFailed(mDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mLightingRootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = { vsLighting->GetBufferPointer(), vsLighting->GetBufferSize() };
    psoDesc.PS = { psLighting->GetBufferPointer(), psLighting->GetBufferSize() };
    psoDesc.pRootSignature = mLightingRootSignature.Get();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO)));

    mLightingCB = std::make_unique<UploadBuffer<LightConstants>>(mDevice, 16, true);
    return true;
}

void RenderingSystem::GeometryPass(
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
    const D3D12_RECT& scissorRect)
{
    if (!mGBuffer)
    {
        return;
    }

    ThrowIfFailed(mCommandAllocator->Reset());
    ThrowIfFailed(mCommandList->Reset(mCommandAllocator, geometryPSO));

    D3D12_RESOURCE_BARRIER gbufferToRT[3] = {
        MakeTransition(mGBuffer->GetTexture(GBuffer::GBUFFER_ALBEDO), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        MakeTransition(mGBuffer->GetTexture(GBuffer::GBUFFER_NORMAL), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        MakeTransition(mGBuffer->GetTexture(GBuffer::GBUFFER_DEPTH), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    mCommandList->ResourceBarrier(3, gbufferToRT);

    D3D12_RESOURCE_BARRIER depthBarrier = MakeTransition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &depthBarrier);

    mGBuffer->ClearRenderTargets(mCommandList);
    mCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] = {
        mGBuffer->GetRTV(GBuffer::GBUFFER_ALBEDO),
        mGBuffer->GetRTV(GBuffer::GBUFFER_NORMAL),
        mGBuffer->GetRTV(GBuffer::GBUFFER_DEPTH)
    };
    mCommandList->OMSetRenderTargets(3, rtvs, FALSE, &dsvHandle);
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);
    mCommandList->SetGraphicsRootSignature(geometryRootSignature);

    ID3D12DescriptorHeap* heaps[] = { sceneHeap };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);

    for (UINT submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex)
    {
        const auto& sm = submeshes[submeshIndex];
        const Material* mat = nullptr;

        for (const auto& material : materials)
        {
            if (material.Name == sm.MaterialName)
            {
                mat = &material;
                break;
            }
        }

        if (!mat)
        {
            continue;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle = sceneHeap->GetGPUDescriptorHandleForHeapStart();
        cbvHandle.ptr += submeshIndex * descriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = sceneHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle.ptr += (materialSrvOffset + mat->SrvHeapIndex) * descriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle);

        mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
    }

    D3D12_RESOURCE_BARRIER gbufferToSRV[3] = {
        MakeTransition(mGBuffer->GetTexture(GBuffer::GBUFFER_ALBEDO), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        MakeTransition(mGBuffer->GetTexture(GBuffer::GBUFFER_NORMAL), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        MakeTransition(mGBuffer->GetTexture(GBuffer::GBUFFER_DEPTH), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    mCommandList->ResourceBarrier(3, gbufferToSRV);

    depthBarrier = MakeTransition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_READ);
    mCommandList->ResourceBarrier(1, &depthBarrier);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();
}

void RenderingSystem::LightingPass(
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
    float farZ)
{
    ThrowIfFailed(mCommandAllocator->Reset());
    ThrowIfFailed(mCommandList->Reset(mCommandAllocator, mLightingPSO.Get()));

    D3D12_RESOURCE_BARRIER barrier = MakeTransition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, nullptr);

    mCommandList->SetGraphicsRootSignature(mLightingRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mGBuffer->GetSrvHeap() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, mGBuffer->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootConstantBufferView(2, cameraCB->Resource()->GetGPUVirtualAddress());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const D3D12_GPU_VIRTUAL_ADDRESS baseAddr = mLightingCB->Resource()->GetGPUVirtualAddress();
    const UINT elementSize = mLightingCB->GetElementSize();

    for (UINT i = 0; i < lights.size(); ++i)
    {
        LightConstants lightConstants;
        lightConstants.SetFromLight(lights[i], cameraPos);
        mLightingCB->CopyData(i, lightConstants);

        const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = baseAddr + i * elementSize;
        mCommandList->SetGraphicsRootConstantBufferView(1, cbAddr);
        mCommandList->DrawInstanced(3, 1, 0, 0);
    }

    RenderDebugOverlays(viewport, nearZ, farZ);

    barrier = MakeTransition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    swapChain->Present(0, 0);
    currBackBufferIndex = (currBackBufferIndex + 1) % mSwapChainBufferCount;
}

void RenderingSystem::Shutdown()
{
    FlushCommandQueue();

    if (mGBuffer)
    {
        mGBuffer->Shutdown();
        mGBuffer.reset();
    }

    mLightingCB.reset();
    mLightingPSO.Reset();
    mLightingRootSignature.Reset();
    mDebugPSO.Reset();
    mDebugRootSignature.Reset();
}

void RenderingSystem::FlushCommandQueue()
{
    ++mFenceValue;
    mCommandQueue->Signal(mFence, mFenceValue);

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(mFenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

bool RenderingSystem::CreateDebugResources()
{
    auto vsDebug = d3dUtil::CompileShader(L"../src/debug.hlsl", nullptr, "VS", "vs_5_0");
    auto psDebug = d3dUtil::CompileShader(L"../src/debug.hlsl", nullptr, "PS", "ps_5_0");
    if (!vsDebug || !psDebug)
    {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.Num32BitValues = 3;
    rootParams[1].Constants.ShaderRegister = 0;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf()));

    ThrowIfFailed(mDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mDebugRootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = { vsDebug->GetBufferPointer(), vsDebug->GetBufferSize() };
    psoDesc.PS = { psDebug->GetBufferPointer(), psDebug->GetBufferSize() };
    psoDesc.pRootSignature = mDebugRootSignature.Get();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDebugPSO)));

    return true;
}

void RenderingSystem::RenderDebugOverlays(const D3D12_VIEWPORT& fullViewport, float nearZ, float farZ)
{
    if (!mDebugPSO || !mGBuffer)
    {
        return;
    }

    mCommandList->SetPipelineState(mDebugPSO.Get());
    mCommandList->SetGraphicsRootSignature(mDebugRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { mGBuffer->GetSrvHeap() };
    mCommandList->SetDescriptorHeaps(1, heaps);

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const float w = fullViewport.Width;
    const float h = fullViewport.Height;

    const float margin = 12.0f;
    const float gap = 8.0f;
    const float minSize = 64.0f;
    const float proportionalSize = (w * 0.18f < h * 0.28f) ? (w * 0.18f) : (h * 0.28f);
    const float size = (proportionalSize > minSize) ? proportionalSize : minSize;

    const float thumbCount = static_cast<float>(GBuffer::GBUFFER_COUNT);
    const float totalWidth = thumbCount * size + (thumbCount - 1.0f) * gap;
    const float startX = w - margin - totalWidth;
    const float y = h - margin - size;

    for (UINT i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        D3D12_VIEWPORT thumbViewport = {};
        thumbViewport.TopLeftX = startX + i * (size + gap);
        thumbViewport.TopLeftY = y;
        thumbViewport.Width = size;
        thumbViewport.Height = size;
        thumbViewport.MinDepth = 0.0f;
        thumbViewport.MaxDepth = 1.0f;

        D3D12_RECT thumbScissor = {
            static_cast<LONG>(thumbViewport.TopLeftX),
            static_cast<LONG>(thumbViewport.TopLeftY),
            static_cast<LONG>(thumbViewport.TopLeftX + thumbViewport.Width),
            static_cast<LONG>(thumbViewport.TopLeftY + thumbViewport.Height)
        };

        mCommandList->RSSetViewports(1, &thumbViewport);
        mCommandList->RSSetScissorRects(1, &thumbScissor);

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = mGBuffer->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        srvHandle.ptr += i * mGBuffer->GetSrvDescriptorSize();
        mCommandList->SetGraphicsRootDescriptorTable(0, srvHandle);

        UINT debugConstants[3] = { i, 0, 0 };
        memcpy(&debugConstants[1], &nearZ, sizeof(float));
        memcpy(&debugConstants[2], &farZ, sizeof(float));
        mCommandList->SetGraphicsRoot32BitConstants(1, 3, debugConstants, 0);

        mCommandList->DrawInstanced(3, 1, 0, 0);
    }
}
