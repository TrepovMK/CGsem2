#include "../h/GBuffer.h"
#include "../h/ThrowIfFailed.h"

bool GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mCbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return CreateTextures(device) && CreateRTVs(device);
}

void GBuffer::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    if (mWidth == width && mHeight == height) return;
    mWidth = width;
    mHeight = height;
    for (auto& texture : mTextures) texture.Reset();
    CreateTextures(device);
    CreateRTVs(device);
}

bool GBuffer::CreateTextures(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = mWidth;
    texDesc.Height = mHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};

    texDesc.Format = mAlbedoFormat;
    clearValue.Format = mAlbedoFormat;
    clearValue.Color[3] = 1.0f;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&mTextures[GBUFFER_ALBEDO])));

    texDesc.Format = mNormalFormat;
    clearValue = {};
    clearValue.Format = mNormalFormat;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&mTextures[GBUFFER_NORMAL])));

    texDesc.Format = mDepthFormat;
    clearValue = {};
    clearValue.Format = mDepthFormat;
    clearValue.Color[0] = 1.0f;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&mTextures[GBUFFER_DEPTH])));

    return true;
}

bool GBuffer::CreateRTVs(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = GBUFFER_COUNT;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = (i == GBUFFER_ALBEDO) ? mAlbedoFormat :
                         (i == GBUFFER_NORMAL) ? mNormalFormat : mDepthFormat;
        device->CreateRenderTargetView(mTextures[i].Get(), &rtvDesc, handle);
        handle.ptr += mRtvDescriptorSize;
    }

    return true;
}

void GBuffer::CreateSrvs(ID3D12Device* device,
                         D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
                         unsigned int gpuStartIndex,
                         unsigned int descriptorSize)
{
    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Format = (i == GBUFFER_ALBEDO) ? mAlbedoFormat :
                         (i == GBUFFER_NORMAL) ? mNormalFormat : mDepthFormat;

        D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuStart;
        handle.ptr += static_cast<SIZE_T>(i) * descriptorSize;
        device->CreateShaderResourceView(mTextures[i].Get(), &srvDesc, handle);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetRTV(GBUFFER_TEXTURE_TYPE type) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += type * mRtvDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetSRV(GBUFFER_TEXTURE_TYPE type) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += type * mCbvSrvDescriptorSize;
    return handle;
}

void GBuffer::ClearRenderTargets(
    ID3D12GraphicsCommandList* cmdList,
    const float* clearColorAlbedo,
    const float* clearColorNormal,
    const float* clearColorDepth)
{
    const float defaultAlbedo[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float defaultNormal[] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const float defaultDepth[] = { 1.0f, 1.0f, 1.0f, 1.0f };

    cmdList->ClearRenderTargetView(GetRTV(GBUFFER_ALBEDO), clearColorAlbedo ? clearColorAlbedo : defaultAlbedo, 0, nullptr);
    cmdList->ClearRenderTargetView(GetRTV(GBUFFER_NORMAL), clearColorNormal ? clearColorNormal : defaultNormal, 0, nullptr);
    cmdList->ClearRenderTargetView(GetRTV(GBUFFER_DEPTH), clearColorDepth ? clearColorDepth : defaultDepth, 0, nullptr);
}

void GBuffer::Shutdown()
{
    for (auto& texture : mTextures)
    {
        texture.Reset();
    }
    mRtvHeap.Reset();
}
