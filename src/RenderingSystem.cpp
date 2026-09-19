#include "../h/RenderingSystem.h"
#include "../h/ThrowIfFailed.h"
#include "../h/d3dUtil.h"
#include <algorithm>

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
    mWidth = width;
    mHeight = height;
    return CreateGBuffer(width, height) && CreateLightingResources() && CreateShadowResources() && CreateParticleResources() && CreatePostResources();
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

    D3D12_DESCRIPTOR_RANGE gbufferRanges[4] = {};
    for (UINT i = 0; i < 4; ++i)
    {
        gbufferRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRanges[i].NumDescriptors = 1;
        gbufferRanges[i].BaseShaderRegister = i;
        gbufferRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER rootParams[4] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 4;
    rootParams[0].DescriptorTable.pDescriptorRanges = gbufferRanges;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].Descriptor.ShaderRegister = 2;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;

    // Comparison-сэмплер для PCF через SampleCmp (как в референсе).
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 4;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 2;
    rootSigDesc.pStaticSamplers = samplers;
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

bool RenderingSystem::CreateShadowResources()
{
    // Root signature теневого прохода: один CBV b0 (ObjectConstants).
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParam;
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
        IID_PPV_ARGS(&mShadowRootSignature)));

    auto vsShadow = d3dUtil::CompileShader(L"../src/shaders.hlsl", nullptr, "VS_Shadow", "vs_5_0");
    if (!vsShadow)
    {
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC shadowInputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = { vsShadow->GetBufferPointer(), vsShadow->GetBufferSize() };
    psoDesc.pRootSignature = mShadowRootSignature.Get();
    psoDesc.InputLayout = { shadowInputLayout, sizeof(shadowInputLayout) / sizeof(shadowInputLayout[0]) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    // Смещение глубины против acne-полос: константное + по наклону полигона.
    psoDesc.RasterizerState.DepthBias = 1000;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mShadowPSO)));
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
    UploadBuffer<ShadowConstants>* shadowCB,
    float nearZ,
    float farZ,
    ID3D12Resource* depthStencilBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    ID3D12DescriptorHeap* particleHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrv,
    D3D12_GPU_VIRTUAL_ADDRESS particleRenderCB,
    UINT particleCount)
{
    (void)nearZ;
    (void)farZ;

    ThrowIfFailed(mCommandAllocator->Reset());
    ThrowIfFailed(mCommandList->Reset(mCommandAllocator, mLightingPSO.Get()));

    // Lighting (аддитивный) рисуется не в backbuffer, а в промежуточную
    // сцену — её потом читает пост-проход как t0 вместе с G-Buffer.
    D3D12_RESOURCE_BARRIER toSceneRT = MakeTransition(
        mSceneTexture.Get(),
        mSceneState,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &toSceneRT);
    mSceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    const float clearScene[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(mSceneRtv, clearScene, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &mSceneRtv, TRUE, nullptr);

    mCommandList->SetGraphicsRootSignature(mLightingRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mGBuffer->GetSrvHeap() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, mGBuffer->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootConstantBufferView(2, cameraCB->Resource()->GetGPUVirtualAddress());
    if (shadowCB)
    {
        mCommandList->SetGraphicsRootConstantBufferView(3, shadowCB->Resource()->GetGPUVirtualAddress());
    }

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

    // Частицы тоже рисуются в сцену, чтобы на них действовали пост-эффекты.
    DrawParticles(
        mSceneTexture.Get(),
        mSceneRtv,
        depthStencilBuffer,
        dsvHandle,
        particleHeap,
        particleSrv,
        particleRenderCB,
        particleCount,
        viewport,
        scissorRect);

    D3D12_RESOURCE_BARRIER toSceneSRV = MakeTransition(
        mSceneTexture.Get(),
        mSceneState,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &toSceneSRV);
    mSceneState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Пост-проход: fullscreen quad без вершинного буфера в backbuffer.
    D3D12_RESOURCE_BARRIER barrier = MakeTransition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    const float clearBack[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearBack, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, nullptr);
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    DrawPostEffects();

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

bool RenderingSystem::CreatePostResources()
{
    // Root signature пост-прохода: таблица из 4 SRV (t0 сцена, t1-t3 G-Buffer)
    // + CBV b0 (PostConstants), статический linear-clamp сэмплер s0.
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC linearClamp = {};
    linearClamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearClamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.ShaderRegister = 0;
    linearClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    linearClamp.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &linearClamp;
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
        IID_PPV_ARGS(&mPostRootSignature)));

    auto vsPost = d3dUtil::CompileShader(L"../src/post_effects.hlsl", nullptr, "VS_FullscreenQuad", "vs_5_0");
    auto psPost = d3dUtil::CompileShader(L"../src/post_effects.hlsl", nullptr, "PS_PostEffects", "ps_5_0");
    if (!vsPost || !psPost)
    {
        return false;
    }

    // PSO пост-прохода: без вершинного буфера, без глубины, один RT.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = { vsPost->GetBufferPointer(), vsPost->GetBufferSize() };
    psoDesc.PS = { psPost->GetBufferPointer(), psPost->GetBufferSize() };
    psoDesc.pRootSignature = mPostRootSignature.Get();
    psoDesc.InputLayout = { nullptr, 0 };
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
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPostPSO)));

    mPostCB = std::make_unique<UploadBuffer<PostConstants>>(mDevice, 1, true);
    UpdatePostConstants(0.0f);

    return CreateSceneTexture();
}

bool RenderingSystem::CreateSceneTexture()
{
    const UINT width = (std::max<UINT>)(mWidth, 1u);
    const UINT height = (std::max<UINT>)(mHeight, 1u);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mSceneRtvHeap)));

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = mBackBufferFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = mBackBufferFormat;
    clear.Color[0] = 0.0f;
    clear.Color[1] = 0.0f;
    clear.Color[2] = 0.0f;
    clear.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    mSceneState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        mSceneState,
        &clear,
        IID_PPV_ARGS(&mSceneTexture)));

    mSceneRtv = mSceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
    mDevice->CreateRenderTargetView(mSceneTexture.Get(), nullptr, mSceneRtv);

    // Куча SRV пост-прохода: t0 сцена, t1-t3 G-Buffer (albedo/normal/depth).
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 4;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mPostSrvHeap)));

    const UINT srvSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mPostSrvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC sceneSrv = {};
    sceneSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sceneSrv.Format = mBackBufferFormat;
    sceneSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sceneSrv.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(mSceneTexture.Get(), &sceneSrv, cpuHandle);
    cpuHandle.ptr += srvSize;

    const GBuffer::GBUFFER_TEXTURE_TYPE gbufferTypes[3] = {
        GBuffer::GBUFFER_ALBEDO, GBuffer::GBUFFER_NORMAL, GBuffer::GBUFFER_DEPTH
    };
    for (int i = 0; i < 3; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = GBuffer::GetFormat(gbufferTypes[i]);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        mDevice->CreateShaderResourceView(mGBuffer->GetTexture(gbufferTypes[i]), &srvDesc, cpuHandle);
        cpuHandle.ptr += srvSize;
    }

    return true;
}

void RenderingSystem::DrawPostEffects()
{
    mCommandList->SetPipelineState(mPostPSO.Get());
    mCommandList->SetGraphicsRootSignature(mPostRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { mPostSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, mPostSrvHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootConstantBufferView(1, mPostCB->Resource()->GetGPUVirtualAddress());

    // Вершинного буфера нет — quad генерирует VS_FullscreenQuad из SV_VertexID.
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    mCommandList->DrawInstanced(4, 1, 0, 0);
}

void RenderingSystem::UpdatePostConstants(float totalTime)
{
    if (!mPostCB)
    {
        return;
    }

    PostConstants post;
    post.InvRenderTargetSize = DirectX::XMFLOAT2(
        1.0f / static_cast<float>((std::max<UINT>)(mWidth, 1u)),
        1.0f / static_cast<float>((std::max<UINT>)(mHeight, 1u)));
    post.Time = totalTime;
    post.ChromaticStrength = mChromaticStrength;
    post.VignetteStrength = mVignetteStrength;
    post.OutlineStrength = mOutlineStrength;
    post.OutlineThreshold = mOutlineThreshold;
    mPostCB->CopyData(0, post);
}

void RenderingSystem::SetPostStrengths(float chromaticStrength, float vignetteStrength, float outlineStrength, float outlineThreshold)
{
    mChromaticStrength = chromaticStrength;
    mVignetteStrength = vignetteStrength;
    mOutlineStrength = outlineStrength;
    mOutlineThreshold = outlineThreshold;
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
    mShadowPSO.Reset();
    mShadowRootSignature.Reset();
    mParticleComputePSO.Reset();
    mParticleGraphicsPSO.Reset();
    mParticleRootSignature.Reset();
    mPostPSO.Reset();
    mPostRootSignature.Reset();
    mPostCB.reset();
    mSceneTexture.Reset();
    mSceneRtvHeap.Reset();
    mPostSrvHeap.Reset();
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

bool RenderingSystem::CreateParticleResources()
{
    // Root signature частиц: SRV t0, UAV u0 (Consume), UAV u1 (Append), CBV b0 (sim), CBV b1 (render).
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE consumeRange = {};
    consumeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    consumeRange.NumDescriptors = 1;
    consumeRange.BaseShaderRegister = 0;
    consumeRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE appendRange = {};
    appendRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    appendRange.NumDescriptors = 1;
    appendRange.BaseShaderRegister = 1;
    appendRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[5] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &consumeRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &appendRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].Descriptor.ShaderRegister = 0;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[4].Descriptor.ShaderRegister = 1;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 5;
    rootSigDesc.pParameters = rootParams;
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
        IID_PPV_ARGS(&mParticleRootSignature)));

    auto vsParticle = d3dUtil::CompileShader(L"../src/particles.hlsl", nullptr, "VS_Particle", "vs_5_0");
    auto gsParticle = d3dUtil::CompileShader(L"../src/particles.hlsl", nullptr, "GS_Particle", "gs_5_0");
    auto psParticle = d3dUtil::CompileShader(L"../src/particles.hlsl", nullptr, "PS_Particle", "ps_5_0");
    auto csParticle = d3dUtil::CompileShader(L"../src/particles.hlsl", nullptr, "CS_UpdateParticles", "cs_5_0");
    if (!vsParticle || !gsParticle || !psParticle || !csParticle)
    {
        return false;
    }

    // Непрозрачные частицы: бленд выключен, глубина включена с записью.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = { vsParticle->GetBufferPointer(), vsParticle->GetBufferSize() };
    psoDesc.GS = { gsParticle->GetBufferPointer(), gsParticle->GetBufferSize() };
    psoDesc.PS = { psParticle->GetBufferPointer(), psParticle->GetBufferSize() };
    psoDesc.pRootSignature = mParticleRootSignature.Get();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mParticleGraphicsPSO)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC csDesc = {};
    csDesc.pRootSignature = mParticleRootSignature.Get();
    csDesc.CS = { csParticle->GetBufferPointer(), csParticle->GetBufferSize() };
    ThrowIfFailed(mDevice->CreateComputePipelineState(&csDesc, IID_PPV_ARGS(&mParticleComputePSO)));

    return true;
}

void RenderingSystem::DrawParticles(
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    ID3D12Resource* depthStencilBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    ID3D12DescriptorHeap* particleHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrv,
    D3D12_GPU_VIRTUAL_ADDRESS particleRenderCB,
    UINT particleCount,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect)
{
    (void)backBuffer;
    if (particleCount == 0 || !particleHeap || particleRenderCB == 0)
    {
        return;
    }
    if (!mParticleGraphicsPSO || !mParticleRootSignature || !depthStencilBuffer)
    {
        return;
    }

    D3D12_RESOURCE_BARRIER toWrite = MakeTransition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &toWrite);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    mCommandList->SetPipelineState(mParticleGraphicsPSO.Get());
    mCommandList->SetGraphicsRootSignature(mParticleRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { particleHeap };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, particleSrv);
    mCommandList->SetGraphicsRootConstantBufferView(4, particleRenderCB);

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    mCommandList->DrawInstanced(particleCount, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toRead = MakeTransition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_READ);
    mCommandList->ResourceBarrier(1, &toRead);
}
