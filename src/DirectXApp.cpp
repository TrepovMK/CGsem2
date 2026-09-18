#include "../h/DirectXApp.h"
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <algorithm>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <string>
#include <filesystem>
#include <limits>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <random>
#include <functional>
#include <fstream>
#include "../h/DDSTextureLoader.h"

#include "../h/model_loader.h"
#include "../h/d3dUtil.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;

namespace
{
    std::string ToLowerAscii(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    void AppendMeshData(MeshData& dst, const MeshData& src) {
        const unsigned int vertexOffset = static_cast<unsigned int>(dst.vertices.size());
        const unsigned int indexOffset = static_cast<unsigned int>(dst.indices.size());

        dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());

        dst.indices.reserve(dst.indices.size() + src.indices.size());
        for (unsigned int idx : src.indices) {
            dst.indices.push_back(idx + vertexOffset);
        }

        dst.submeshes.reserve(dst.submeshes.size() + src.submeshes.size());
        for (auto sm : src.submeshes) {
            sm.startIndexLocation += indexOffset;
            sm.baseVertexLocation = 0;
            dst.submeshes.push_back(sm);
        }
    }

    struct MeshBounds {
        XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 extents = {1.0f, 1.0f, 1.0f};
        float radius = 1.0f;
    };

    MeshBounds ComputeMeshBounds(const MeshData& mesh) {
        MeshBounds bounds{};
        if (mesh.vertices.empty()) {
            return bounds;
        }

        XMFLOAT3 vMin(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        XMFLOAT3 vMax(
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max());

        for (const auto& v : mesh.vertices) {
            vMin.x = (std::min)(vMin.x, v.Position.x);
            vMin.y = (std::min)(vMin.y, v.Position.y);
            vMin.z = (std::min)(vMin.z, v.Position.z);

            vMax.x = (std::max)(vMax.x, v.Position.x);
            vMax.y = (std::max)(vMax.y, v.Position.y);
            vMax.z = (std::max)(vMax.z, v.Position.z);
        }

        bounds.center = XMFLOAT3(
            0.5f * (vMin.x + vMax.x),
            0.5f * (vMin.y + vMax.y),
            0.5f * (vMin.z + vMax.z));
        bounds.extents = XMFLOAT3(
            0.5f * (vMax.x - vMin.x),
            0.5f * (vMax.y - vMin.y),
            0.5f * (vMax.z - vMin.z));

        float radius = 0.0f;
        for (const auto& v : mesh.vertices) {
            const float dx = v.Position.x - bounds.center.x;
            const float dy = v.Position.y - bounds.center.y;
            const float dz = v.Position.z - bounds.center.z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            radius = (std::max)(radius, d);
        }

        bounds.radius = (radius > 1e-5f) ? radius : 1.0f;
        return bounds;
    }

    void NormalizeMeshToRadius(MeshData& mesh, float targetRadius, float yOffset) {
        const MeshBounds originalBounds = ComputeMeshBounds(mesh);
        const float normalizeScale = (originalBounds.radius > 1e-5f) ? (targetRadius / originalBounds.radius) : 1.0f;

        for (auto& v : mesh.vertices) {
            v.Position.x = (v.Position.x - originalBounds.center.x) * normalizeScale;
            v.Position.y = (v.Position.y - originalBounds.center.y) * normalizeScale + yOffset;
            v.Position.z = (v.Position.z - originalBounds.center.z) * normalizeScale;
        }
    }

    ComPtr<ID3D12Resource> CreateDefaultBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const void* initData,
        UINT64 byteSize,
        ComPtr<ID3D12Resource>& uploadBuffer) {
        ComPtr<ID3D12Resource> defaultBuffer;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = byteSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&defaultBuffer)));

        D3D12_HEAP_PROPERTIES uploadProps = {};
        uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        ThrowIfFailed(device->CreateCommittedResource(
            &uploadProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer)));

        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = initData;
        subData.RowPitch = static_cast<LONG_PTR>(byteSize);
        subData.SlicePitch = subData.RowPitch;

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = defaultBuffer.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);

        UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subData);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
        cmdList->ResourceBarrier(1, &barrier);

        return defaultBuffer;
    }

    bool LoadTgaTextureFromFile12(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filePath,
        ComPtr<ID3D12Resource>& texture,
        ComPtr<ID3D12Resource>& uploadHeap) {
        std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
        if (!file) {
            return false;
        }

        unsigned char header[18] = {};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!file) {
            return false;
        }

        const unsigned char idLength = header[0];
        const unsigned char colorMapType = header[1];
        const unsigned char imageType = header[2];
        const unsigned short width = static_cast<unsigned short>(header[12] | (header[13] << 8));
        const unsigned short height = static_cast<unsigned short>(header[14] | (header[15] << 8));
        const unsigned char bpp = header[16];
        const unsigned char descriptor = header[17];

        if (colorMapType != 0 || width == 0 || height == 0) {
            return false;
        }
        const bool supportedType = (imageType == 2 || imageType == 3 || imageType == 10 || imageType == 11);
        if (!supportedType) {
            return false;
        }
        if (bpp != 8 && bpp != 24 && bpp != 32) {
            return false;
        }

        if (idLength > 0) {
            file.seekg(idLength, std::ios::cur);
            if (!file) {
                return false;
            }
        }

        const unsigned int pixelCount = static_cast<unsigned int>(width) * static_cast<unsigned int>(height);
        std::vector<unsigned char> rgba(pixelCount * 4u, 255u);

        const auto writePixel = [&](unsigned int pixelIndex, const unsigned char* src) {
            const unsigned int dst = pixelIndex * 4u;
            if (bpp == 32) {
                rgba[dst + 0] = src[2];
                rgba[dst + 1] = src[1];
                rgba[dst + 2] = src[0];
                rgba[dst + 3] = src[3];
            } else if (bpp == 24) {
                rgba[dst + 0] = src[2];
                rgba[dst + 1] = src[1];
                rgba[dst + 2] = src[0];
                rgba[dst + 3] = 255;
            } else {
                rgba[dst + 0] = src[0];
                rgba[dst + 1] = src[0];
                rgba[dst + 2] = src[0];
                rgba[dst + 3] = 255;
            }
        };

        const unsigned int bytesPerPixel = bpp / 8u;
        std::vector<unsigned char> pixel(bytesPerPixel);
        unsigned int pixelIndex = 0;

        if (imageType == 2 || imageType == 3) {
            while (pixelIndex < pixelCount) {
                file.read(reinterpret_cast<char*>(pixel.data()), bytesPerPixel);
                if (!file) {
                    return false;
                }
                writePixel(pixelIndex, pixel.data());
                ++pixelIndex;
            }
        } else {
            while (pixelIndex < pixelCount) {
                unsigned char packetHeader = 0;
                file.read(reinterpret_cast<char*>(&packetHeader), 1);
                if (!file) {
                    return false;
                }

                const unsigned int runLength = (packetHeader & 0x7Fu) + 1u;
                if (packetHeader & 0x80u) {
                    file.read(reinterpret_cast<char*>(pixel.data()), bytesPerPixel);
                    if (!file) {
                        return false;
                    }
                    for (unsigned int i = 0; i < runLength && pixelIndex < pixelCount; ++i, ++pixelIndex) {
                        writePixel(pixelIndex, pixel.data());
                    }
                } else {
                    for (unsigned int i = 0; i < runLength && pixelIndex < pixelCount; ++i, ++pixelIndex) {
                        file.read(reinterpret_cast<char*>(pixel.data()), bytesPerPixel);
                        if (!file) {
                            return false;
                        }
                        writePixel(pixelIndex, pixel.data());
                    }
                }
            }
        }

        const bool topOrigin = (descriptor & 0x20u) != 0u;
        if (!topOrigin) {
            const unsigned int rowPitch = static_cast<unsigned int>(width) * 4u;
            std::vector<unsigned char> flipped(rgba.size());
            for (unsigned int y = 0; y < static_cast<unsigned int>(height); ++y) {
                const unsigned int srcOffset = (static_cast<unsigned int>(height) - 1u - y) * rowPitch;
                const unsigned int dstOffset = y * rowPitch;
                std::copy_n(rgba.data() + srcOffset, rowPitch, flipped.data() + dstOffset);
            }
            rgba.swap(flipped);
        }

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES defaultHeapProps = {};
        defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        if (FAILED(device->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)))) {
            return false;
        }

        const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadDesc = {};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = uploadSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadHeap)))) {
            return false;
        }

        const UINT rowPitch = static_cast<UINT>(width) * 4u;
        const UINT imageSize = rowPitch * static_cast<UINT>(height);
        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = rgba.data();
        subresource.RowPitch = rowPitch;
        subresource.SlicePitch = imageSize;

        UpdateSubresources(cmdList, texture.Get(), uploadHeap.Get(), 0, 0, 1, &subresource);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);

        return true;
    }
}

DirectXApp::DirectXApp(Window& window) : window(window)
{
}

DirectXApp::~DirectXApp()
{
    Shutdown();
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

void DirectXApp::OnKeyDown(WPARAM wParam)
{
    if (GetActiveWindow() != window.GetHwnd())
    {
        return;
    }

    if (wParam == 'T')
    {
        mAnimateTextures = !mAnimateTextures;
        mTitleDirty = true;
    }

    if (wParam == 'R')
    {
        mTexScaleU = 1.0f;
        mTexScaleV = 1.0f;
        mTexAnimU = 0.0f;
        mTexAnimV = 0.0f;
        mTitleDirty = true;
    }

    if (wParam == VK_F1) { mDebugViewMode = 1; mTitleDirty = true; }
    if (wParam == VK_F2) { mDebugViewMode = 2; mTitleDirty = true; }
    if (wParam == VK_F3) { mDebugViewMode = 3; mTitleDirty = true; }
}

void DirectXApp::BuildScene()
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    LoadModels();
    BuildGeometryBuffers();
    LoadTextures();
    CreateFallbackTextures();

    BuildScenePresets();
    ActivateScene(0, false);

    BuildConstantBuffers();
    BuildMainSrvHeap();
    BindSubmeshTextures();
    BuildLights();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();

    mVertexBufferUploader.Reset();
    mIndexBufferUploader.Reset();
}

void DirectXApp::LoadModels()
{
    mSceneMesh = {};
    mModelAssets.clear();

    struct ModelLoadSpec {
        std::string name;
        std::vector<std::string> candidates;
        float targetRadius = 1.0f;
        float yOffset = 0.0f;
        bool forceEarthMaterial = false;
    };

    const std::vector<ModelLoadSpec> loadSpecs = {
        {
            "earth",
            {"../assets/Earth.fbx", "../assets/earth.fbx"},
            3.5f,
            0.0f,
            true
        },
        {
            "sponza",
            {"../assets/sponza/sponza.obj", "../assets/sponza.obj"},
            18.0f,
            0.0f,
            false
        }
    };

    for (const auto& spec : loadSpecs) {
        std::string resolvedPath;
        bool found = false;
        for (const auto& candidate : spec.candidates) {
            if (std::filesystem::exists(candidate)) {
                resolvedPath = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            std::string warn = "Model not found: " + spec.name + "\n";
            OutputDebugStringA(warn.c_str());
            continue;
        }

        auto mesh = ModelLoader::LoadModel(resolvedPath, XMMatrixIdentity());
        NormalizeMeshToRadius(mesh, spec.targetRadius, spec.yOffset);

        if (spec.forceEarthMaterial) {
            for (auto& sm : mesh.submeshes) {
                sm.material.diffuseTextureName = "Earth_ALB";
                sm.material.normalTextureName = "Earth_NORM";
                sm.material.displacementTextureName = "Earth_HEIGHT";
                sm.material.shininess = 64.0f;
            }
        }

        const MeshBounds bounds = ComputeMeshBounds(mesh);
        ModelAsset asset;
        asset.name = spec.name;
        asset.startIndex = static_cast<unsigned int>(mSceneMesh.submeshes.size());
        asset.submeshCount = static_cast<unsigned int>(mesh.submeshes.size());
        asset.localCenter = bounds.center;
        asset.localExtents = bounds.extents;
        asset.localRadius = bounds.radius;
        mModelAssets.push_back(asset);

        AppendMeshData(mSceneMesh, mesh);

        std::string msg = "Loaded model: " + spec.name + " submeshes=" + std::to_string(asset.submeshCount) + "\n";
        OutputDebugStringA(msg.c_str());
    }

    if (mModelAssets.empty()) {
        throw std::runtime_error("No models loaded");
    }
}

void DirectXApp::BuildScenePresets()
{
    mScenePresets.clear();

    auto findModelIndex = [&](const std::string& modelName) -> unsigned int {
        const std::string key = ToLowerAscii(modelName);
        for (unsigned int i = 0; i < static_cast<unsigned int>(mModelAssets.size()); ++i) {
            if (ToLowerAscii(mModelAssets[i].name) == key) {
                return i;
            }
        }
        return 0;
    };

    const unsigned int earthModel = findModelIndex("earth");
    const unsigned int sponzaModel = findModelIndex("sponza");
    const bool hasSponza = ToLowerAscii(mModelAssets[sponzaModel].name) == "sponza";

    ScenePreset scene1;
    scene1.name = L"Scene 1: Single Earth";
    scene1.cameraPos = XMFLOAT3(0.0f, 2.8f, -15.0f);
    scene1.cameraYaw = 0.0f;
    scene1.cameraPitch = 0.0f;
    {
        SceneObject earth;
        earth.modelIndex = earthModel;
        earth.position = XMFLOAT3(0.0f, 0.6f, 0.0f);
        earth.scale = 1.0f;
        earth.rotationY = 0.0f;
        scene1.objects.push_back(earth);
    }
    mScenePresets.push_back(scene1);

    ScenePreset scene2;
    scene2.name = L"Scene 2: Multi-Model (81 Earths)";
    scene2.cameraPos = XMFLOAT3(0.0f, 6.5f, -36.0f);
    scene2.cameraYaw = 0.0f;
    scene2.cameraPitch = -0.08f;
    {
        if (hasSponza) {
            SceneObject sponza;
            sponza.modelIndex = sponzaModel;
            sponza.position = XMFLOAT3(0.0f, -6.0f, 0.0f);
            sponza.scale = 1.0f;
            sponza.rotationY = 0.0f;
            scene2.objects.push_back(sponza);
        }

        const int rows = 9;
        const int cols = 9;
        const float spacing = 7.0f;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                SceneObject earth;
                earth.modelIndex = earthModel;
                earth.position = XMFLOAT3(
                    (static_cast<float>(c) - (cols - 1) * 0.5f) * spacing,
                    1.0f + ((r + c) % 3) * 0.35f,
                    (static_cast<float>(r) - (rows - 1) * 0.5f) * spacing);
                earth.scale = 0.35f + 0.1f * static_cast<float>((r + c) % 4);
                earth.rotationY = 0.25f * static_cast<float>((r * cols + c) % 7);
                scene2.objects.push_back(earth);
            }
        }
    }
    mScenePresets.push_back(scene2);

    ScenePreset scene3;
    scene3.name = L"Scene 3: Stress Test (1000 Earths)";
    scene3.cameraPos = XMFLOAT3(0.0f, 8.5f, -46.0f);
    scene3.cameraYaw = 0.0f;
    scene3.cameraPitch = -0.02f;
    {
        if (hasSponza) {
            SceneObject sponza;
            sponza.modelIndex = sponzaModel;
            sponza.position = XMFLOAT3(0.0f, -5.8f, 0.0f);
            sponza.scale = 0.85f;
            sponza.rotationY = 0.0f;
            scene3.objects.push_back(sponza);
        }

        std::mt19937 rng(20260501u);
        std::uniform_real_distribution<float> xzDist(-55.0f, 55.0f);
        std::uniform_real_distribution<float> yDist(0.0f, 5.0f);
        std::uniform_real_distribution<float> scaleDist(0.12f, 0.3f);
        std::uniform_real_distribution<float> rotDist(0.0f, XM_2PI);

        const unsigned int objectCount = 1000;
        for (unsigned int i = 0; i < objectCount; ++i) {
            SceneObject earth;
            earth.modelIndex = earthModel;
            earth.position = XMFLOAT3(xzDist(rng), yDist(rng), xzDist(rng));
            earth.scale = scaleDist(rng);
            earth.rotationY = rotDist(rng);
            scene3.objects.push_back(earth);
        }
    }
    mScenePresets.push_back(scene3);
}

void DirectXApp::ActivateScene(int index, bool resetCamera)
{
    if (mScenePresets.empty()) {
        return;
    }

    mActiveSceneIndex = (std::min)(static_cast<unsigned int>(index),
                                   static_cast<unsigned int>(mScenePresets.size() - 1));
    const ScenePreset& preset = mScenePresets[mActiveSceneIndex];
    mSceneObjects = preset.objects;

    if (resetCamera) {
        mEyePos = preset.cameraPos;
        mYaw = preset.cameraYaw;
        mPitch = preset.cameraPitch;
    }

    mAnimateTextures = false;
    mTexAnimU = 0.0f;
    mTexAnimV = 0.0f;
    mTexScaleU = 1.0f;
    mTexScaleV = 1.0f;

    RebuildSceneObjectTransforms();
    BuildOctree();
    mVisibleObjects.resize(mSceneObjects.size());
    for (unsigned int i = 0; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
        mVisibleObjects[i] = i;
    }

    BuildConstantBuffers();
    BuildMainSrvHeap();
    BindSubmeshTextures();
    mTitleDirty = true;
}

void DirectXApp::RebuildSceneObjectTransforms()
{
    for (auto& obj : mSceneObjects) {
        const XMMATRIX world =
            XMMatrixScaling(obj.scale, obj.scale, obj.scale) *
            XMMatrixRotationY(obj.rotationY) *
            XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);
        XMStoreFloat4x4(&obj.world, world);

        const ModelAsset& model = mModelAssets[obj.modelIndex];
        const BoundingBox localBounds(model.localCenter, model.localExtents);
        BoundingBox worldBounds;
        localBounds.Transform(worldBounds, world);
        obj.worldBounds = worldBounds;
    }
}

void DirectXApp::BuildOctree()
{
    mOctreeNodes.clear();
    if (mSceneObjects.empty()) {
        return;
    }

    BoundingBox sceneBounds = mSceneObjects[0].worldBounds; // ищет размеры для одного большого бокса для всей сцены
    for (unsigned int i = 1; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
        BoundingBox::CreateMerged(sceneBounds, sceneBounds, mSceneObjects[i].worldBounds);
    }

    XMFLOAT3 center = sceneBounds.Center;
    XMFLOAT3 extents = sceneBounds.Extents;
    extents.x = (std::max)(extents.x, 1.0f);
    extents.y = (std::max)(extents.y, 1.0f);
    extents.z = (std::max)(extents.z, 1.0f);
    extents.x *= 1.001f;
    extents.y *= 1.001f;
    extents.z *= 1.001f; //слегка его увеличивает

    OctreeNode rootNode; // создает
    rootNode.center = center;
    rootNode.extents = extents;
    rootNode.bounds = BoundingBox(center, extents);
    mOctreeNodes.push_back(std::move(rootNode));

    for (unsigned int i = 0; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) { //запускает запихивание объектов туда
        InsertObjectIntoOctree(i, 0, 0);
    }
}

void DirectXApp::InsertObjectIntoOctree(unsigned int objectIndex, int nodeIndex, int depth)
{
    if (depth >= kMaxOctreeDepth) {
        mOctreeNodes[nodeIndex].objectIndices.push_back(objectIndex);
        return;
    }

    const SceneObject& object = mSceneObjects[objectIndex];

    if (static_cast<int>(mOctreeNodes[nodeIndex].objectIndices.size()) >= kMaxLeafObjects && // проверяем, больше ли 28 объектов и уровень глубины
        depth < kMaxOctreeDepth) {
        if (mOctreeNodes[nodeIndex].children[0] == -1) {
            XMFLOAT3 parentCenter = mOctreeNodes[nodeIndex].center;
            XMFLOAT3 childExtents(mOctreeNodes[nodeIndex].extents.x * 0.5f, mOctreeNodes[nodeIndex].extents.y * 0.5f, mOctreeNodes[nodeIndex].extents.z * 0.5f); //считаем размер некст кубика который в два раза меньше прошлого
            for (int i = 0; i < 8; ++i) {
                OctreeNode child; //создаем восемь детев
                child.extents = childExtents;
                child.center = XMFLOAT3(
                    parentCenter.x + ((i & 1) ? childExtents.x : -childExtents.x),
                    parentCenter.y + ((i & 2) ? childExtents.y : -childExtents.y),
                    parentCenter.z + ((i & 4) ? childExtents.z : -childExtents.z)); //положение дитя внутри родительского кубика
                child.bounds = BoundingBox(child.center, child.extents);
                mOctreeNodes[nodeIndex].children[i] = static_cast<int>(mOctreeNodes.size());
                mOctreeNodes.push_back(std::move(child)); // кладем дете в куб и запоминаем его индекс
            }

            auto storedObjects = mOctreeNodes[nodeIndex].objectIndices; // теперь мы достаем объекты из биг куба и будем распихивать по детям
            mOctreeNodes[nodeIndex].objectIndices.clear();
            for (unsigned int idx : storedObjects) {
                InsertObjectIntoOctree(idx, nodeIndex, depth);
            }
        }
    }

    XMFLOAT3 nodeCenter = mOctreeNodes[nodeIndex].center;
    XMFLOAT3 nodeExtents = mOctreeNodes[nodeIndex].extents;
    const XMFLOAT3 childExtents(nodeExtents.x * 0.5f, nodeExtents.y * 0.5f, nodeExtents.z * 0.5f);
    unsigned int targetChild = 0;
    targetChild |= (object.worldBounds.Center.x >= nodeCenter.x) ? 1u : 0u; //тож самое что и при нарезке детей -
    targetChild |= (object.worldBounds.Center.y >= nodeCenter.y) ? 2u : 0u;
    targetChild |= (object.worldBounds.Center.z >= nodeCenter.z) ? 4u : 0u;

    XMFLOAT3 nextCenter(
        nodeCenter.x + ((targetChild & 1) ? childExtents.x : -childExtents.x),
        nodeCenter.y + ((targetChild & 2) ? childExtents.y : -childExtents.y),
        nodeCenter.z + ((targetChild & 4) ? childExtents.z : -childExtents.z));
    BoundingBox childBounds(nextCenter, childExtents); //дальш смотрим какой центр у этого дете и задаем ему баундинг бокс

    if (childBounds.Contains(object.worldBounds) == CONTAINS) { // если объект влезает в ребенка... тогда инсерт в ребенка
        if (mOctreeNodes[nodeIndex].children[targetChild] == -1) {
            OctreeNode child;
            child.extents = childExtents;
            child.center = nextCenter;
            child.bounds = childBounds;
            mOctreeNodes[nodeIndex].children[targetChild] = static_cast<int>(mOctreeNodes.size());
            mOctreeNodes.push_back(std::move(child));
        }
        InsertObjectIntoOctree(objectIndex, mOctreeNodes[nodeIndex].children[targetChild], depth + 1);
    } else {
        mOctreeNodes[nodeIndex].objectIndices.push_back(objectIndex); // иначе пока не трогаем
    }
}

void DirectXApp::CollectVisibleObjects() //лаба 4
{
    mVisibleObjects.clear();
    mObjectsTestedThisFrame = 0;
    mOctreeNodesVisitedThisFrame = 0;

    if (mSceneObjects.empty()) {
        return;
    }

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));
    const XMVECTOR eye = XMLoadFloat3(&mEyePos);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
        static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
        0.1f, 5000.0f);

    if (!mFrustumCullingEnabled) {
        mVisibleObjects.reserve(mSceneObjects.size());
        for (unsigned int i = 0; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
            mVisibleObjects.push_back(i);
        }
        mObjectsTestedThisFrame = static_cast<unsigned int>(mSceneObjects.size());
        return;
    }

    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, proj);
    BoundingFrustum worldFrustum;
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);
    viewFrustum.Transform(worldFrustum, invView);

    if (mOctreeCullingEnabled && !mOctreeNodes.empty()) {
        CollectVisibleFromOctree(0, worldFrustum);
        return;
    }

    mVisibleObjects.reserve(mSceneObjects.size());
    for (unsigned int i = 0; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
        ++mObjectsTestedThisFrame;
        if (worldFrustum.Contains(mSceneObjects[i].worldBounds) != DISJOINT) { //проверяет, попадает ли в фрустум
            mVisibleObjects.push_back(i);
        }
    }
}

void DirectXApp::CollectVisibleFromOctree(int nodeIndex, const BoundingFrustum& frustum)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(mOctreeNodes.size())) {
        return;
    }

    ++mOctreeNodesVisitedThisFrame;
    const OctreeNode& node = mOctreeNodes[nodeIndex];

    if (!frustum.Intersects(node.bounds)) { //если кубик полностью мимо - откидываем всю ветку
        return;
    }

    if (frustum.Contains(node.bounds) == CONTAINS) {  // если кубик полностью внутри, то сразу все его объекты отрисовываем
        std::function<void(int)> gatherAll = [&](int idx) {
            if (idx < 0 || idx >= static_cast<int>(mOctreeNodes.size())) return;
            const OctreeNode& n = mOctreeNodes[idx];
            mObjectsTestedThisFrame += static_cast<unsigned int>(n.objectIndices.size());
            for (unsigned int objIdx : n.objectIndices) {
                mVisibleObjects.push_back(objIdx);
            }
            for (int i = 0; i < 8; ++i) {
                if (n.children[i] != -1) {
                    gatherAll(n.children[i]);
                }
            }
        };
        gatherAll(nodeIndex);
        return;
    }

    for (unsigned int objectIndex : node.objectIndices) {
        ++mObjectsTestedThisFrame;
        if (frustum.Contains(mSceneObjects[objectIndex].worldBounds) != DISJOINT) {
            mVisibleObjects.push_back(objectIndex);
        }
    } //отрисовываем что не "полностью мимо" в фрустуме

    for (int i = 0; i < 8; ++i) {
        if (node.children[i] != -1) {
            CollectVisibleFromOctree(node.children[i], frustum);
        }
    } //запускаем рекурсию
}

void DirectXApp::UpdateWindowTitle()
{
    std::wostringstream ws;
    ws << L"DirectX 12 Tessellation";
    if (!mScenePresets.empty() && mActiveSceneIndex < mScenePresets.size()) {
        ws << L" | " << mScenePresets[mActiveSceneIndex].name;
    }

    if (mDebugViewMode == 2) {
        ws << L" | F2: Normal Debug";
    } else if (mDebugViewMode == 3) {
        ws << L" | F3: Tess Debug + Wire";
    } else {
        ws << L" | F1: Default";
    }

    ws << L" | T: Anim " << (mAnimateTextures ? L"ON" : L"OFF");
    ws << L" | C: Frustum " << (mFrustumCullingEnabled ? L"ON" : L"OFF");
    ws << L" | O: Octree " << (mOctreeCullingEnabled ? L"ON" : L"OFF");

    ws << L" | Visible " << mVisibleObjects.size() << L"/" << mSceneObjects.size();
    ws << L" | Tested " << mObjectsTestedThisFrame;
    if (mOctreeCullingEnabled) {
        ws << L" | OctNodes " << mOctreeNodesVisitedThisFrame;
    }

    ws << L" | [1-3] Scenes";
    SetWindowTextW(window.GetHandle(), ws.str().c_str());
}

void DirectXApp::BuildGeometryBuffers()
{
    const UINT vbByteSize = static_cast<UINT>(mSceneMesh.vertices.size() * sizeof(Vertex));
    const UINT ibByteSize = static_cast<UINT>(mSceneMesh.indices.size() * sizeof(unsigned int));

    mVertexBufferGPU = CreateDefaultBuffer(device.Get(), mCommandList.Get(), mSceneMesh.vertices.data(), vbByteSize,
                                           mVertexBufferUploader);

    mIndexBufferGPU = CreateDefaultBuffer(device.Get(), mCommandList.Get(), mSceneMesh.indices.data(), ibByteSize,
                                          mIndexBufferUploader);

    mVertexBufferView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
    mVertexBufferView.StrideInBytes = sizeof(Vertex);
    mVertexBufferView.SizeInBytes = vbByteSize;

    mIndexBufferView.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mIndexBufferView.SizeInBytes = ibByteSize;
}

void DirectXApp::LoadTextures()
{
    mTextureResources.clear();
    mTextureNameToIndex.clear();

    const std::array<std::wstring, 2> dirs = {
        L"../assets/textures/earth",
        L"../assets/textures/sponza"
    };

    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto ext = ToLowerAscii(entry.path().extension().string());
            const bool isDDS = (ext == ".dds");
            const bool isTGA = (ext == ".tga");
            if (!isDDS && !isTGA) {
                continue;
            }

            TextureResource tex;
            tex.path = entry.path().wstring();

            bool loaded = false;
            if (isDDS) {
                const HRESULT hr = DirectX::CreateDDSTextureFromFile12(
                    device.Get(),
                    mCommandList.Get(),
                    tex.path.c_str(),
                    tex.resource,
                    tex.uploadHeap);
                loaded = SUCCEEDED(hr);
            } else if (isTGA) {
                loaded = LoadTgaTextureFromFile12(
                    device.Get(),
                    mCommandList.Get(),
                    tex.path,
                    tex.resource,
                    tex.uploadHeap);
            }

            if (!loaded) {
                continue;
            }

            const std::string name = ToLowerAscii(entry.path().stem().string());
            if (mTextureNameToIndex.find(name) != mTextureNameToIndex.end()) {
                continue;
            }

            const unsigned int newIndex = static_cast<unsigned int>(mTextureResources.size());
            mTextureNameToIndex[name] = newIndex;
            mTextureResources.push_back(std::move(tex));
        }
    }
}

void DirectXApp::CreateFallbackTextures()
{
    auto addSolid = [&](const std::string& key, unsigned int rgba) {
        TextureResource tex;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&tex.resource)));

        const UINT64 uploadSize = GetRequiredIntermediateSize(tex.resource.Get(), 0, 1);

        D3D12_HEAP_PROPERTIES uploadProps = {};
        uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = uploadSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufDesc.SampleDesc.Count = 1;

        ThrowIfFailed(device->CreateCommittedResource(
            &uploadProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&tex.uploadHeap)));

        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = &rgba;
        subresource.RowPitch = 4;
        subresource.SlicePitch = 4;

        UpdateSubresources(mCommandList.Get(), tex.resource.Get(), tex.uploadHeap.Get(), 0, 0, 1, &subresource);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = tex.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        mCommandList->ResourceBarrier(1, &barrier);

        const unsigned int newIndex = static_cast<unsigned int>(mTextureResources.size());
        mTextureNameToIndex[key] = newIndex;
        mTextureResources.push_back(std::move(tex));

        return newIndex;
    };

    mFallbackDiffuseIndex = addSolid("__fallback_diffuse", 0xFFFFFFFFu);
    mFallbackNormalIndex = addSolid("__fallback_normal", 0xFFFF8080u);
    mFallbackDisplacementIndex = addSolid("__fallback_displacement", 0xFF000000u);
}

void DirectXApp::BindSubmeshTextures()
{
    auto resolve = [&](const std::string& rawName, unsigned int fallbackIndex) -> unsigned int {
        const std::string key = ToLowerAscii(rawName);
        auto it = mTextureNameToIndex.find(key);
        if (it == mTextureNameToIndex.end()) {
            return mTextureResources[fallbackIndex].srvHeapIndex;
        }
        return mTextureResources[it->second].srvHeapIndex;
    };

    for (auto& submesh : mSceneMesh.submeshes) {
        submesh.material.diffuseSrvHeapIndex = resolve(submesh.material.diffuseTextureName, mFallbackDiffuseIndex);
        submesh.material.normalSrvHeapIndex = resolve(submesh.material.normalTextureName, mFallbackNormalIndex);

        if (submesh.material.displacementTextureName.empty()) {
            submesh.material.displacementSrvHeapIndex = mTextureResources[mFallbackDisplacementIndex].srvHeapIndex;
        } else {
            submesh.material.displacementSrvHeapIndex =
                resolve(submesh.material.displacementTextureName, mFallbackDisplacementIndex);
        }
    }
}

void DirectXApp::BuildConstantBuffers()
{
    const unsigned int objectCount = (std::max)(1u, static_cast<unsigned int>(mSceneObjects.size()));
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device.Get(), objectCount, true);
    mPassCB = std::make_unique<UploadBuffer<PassConstants>>(device.Get(), 1, true);
    mLightingCB = std::make_unique<UploadBuffer<LightingConstants>>(device.Get(), LightingCbElementCount, true);
}

void DirectXApp::BuildMainSrvHeap()
{
    const unsigned int textureCount = static_cast<unsigned int>(mTextureResources.size());
    const unsigned int objectCbvCount = (std::max)(1u, static_cast<unsigned int>(mSceneObjects.size()));
    const unsigned int descriptorCount = objectCbvCount + 2 + textureCount + GBuffer::GBUFFER_COUNT;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = descriptorCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mCbvHeap)));

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    const unsigned int objectElementSize = mObjectCB->GetElementSize();
    for (unsigned int i = 0; i < objectCbvCount; ++i) {
        cbvDesc.BufferLocation = mObjectCB->Resource()->GetGPUVirtualAddress() + static_cast<UINT64>(i) * objectElementSize;
        cbvDesc.SizeInBytes = objectElementSize;
        device->CreateConstantBufferView(&cbvDesc, cpuHandle);
        cpuHandle.ptr += mCbvSrvUavDescriptorSize;
    }

    cbvDesc.BufferLocation = mPassCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    device->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.ptr += mCbvSrvUavDescriptorSize;

    cbvDesc.BufferLocation = mLightingCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(LightingConstants));
    device->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.ptr += mCbvSrvUavDescriptorSize;

    mTextureSrvStart = objectCbvCount + 2;

    for (unsigned int i = 0; i < textureCount; ++i) {
        auto& tex = mTextureResources[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = tex.resource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = tex.resource->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpuHandle);
        tex.srvHeapIndex = mTextureSrvStart + i;
        cpuHandle.ptr += mCbvSrvUavDescriptorSize;
    }

    mGBufferSrvStart = mTextureSrvStart + textureCount;

    D3D12_CPU_DESCRIPTOR_HANDLE gbufferCpu = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
    gbufferCpu.ptr += static_cast<SIZE_T>(mGBufferSrvStart) * mCbvSrvUavDescriptorSize;

    mRenderingSystem->GetGBuffer()->CreateSrvs(
        device.Get(),
        gbufferCpu,
        mGBufferSrvStart,
        mCbvSrvUavDescriptorSize);
}

void DirectXApp::BuildLights()
{
    mLights.clear();

    LightData dir;
    dir.Type = static_cast<unsigned int>(LightType::Directional);
    dir.Direction = XMFLOAT3(-0.25f, -1.0f, 0.35f);
    dir.Color = XMFLOAT3(1.0f, 0.97f, 0.92f);
    dir.Intensity = 1.1f;
    mLights.push_back(dir);

    auto addPoint = [&](const XMFLOAT3& pos, const XMFLOAT3& color, float intensity, float range) {
        LightData point;
        point.Type = static_cast<unsigned int>(LightType::Point);
        point.Position = pos;
        point.Color = color;
        point.Intensity = intensity;
        point.Range = range;
        mLights.push_back(point);
    };

    addPoint(XMFLOAT3(-10.0f, 5.0f, -2.0f), XMFLOAT3(1.0f, 0.35f, 0.35f), 5.5f, 26.0f);
    addPoint(XMFLOAT3(9.0f, 6.0f, 7.0f), XMFLOAT3(0.3f, 0.55f, 1.0f), 4.8f, 24.0f);
    addPoint(XMFLOAT3(0.0f, 10.0f, -11.0f), XMFLOAT3(0.45f, 1.0f, 0.45f), 3.8f, 28.0f);

    auto addSpot = [&](const XMFLOAT3& pos, const XMFLOAT3& dirVec, const XMFLOAT3& color, float intensity, float range, float angle) {
        LightData spot;
        spot.Type = static_cast<unsigned int>(LightType::Spot);
        spot.Position = pos;
        spot.Direction = dirVec;
        spot.Color = color;
        spot.Intensity = intensity;
        spot.Range = range;
        spot.SpotAngle = angle;
        mLights.push_back(spot);
    };

    addSpot(XMFLOAT3(13.0f, 9.0f, 0.0f), XMFLOAT3(-1.0f, -0.75f, 0.0f), XMFLOAT3(1.0f, 0.9f, 0.5f), 2.2f, 34.0f, 0.35f);
    addSpot(XMFLOAT3(-13.0f, 8.0f, 3.0f), XMFLOAT3(1.0f, -0.85f, -0.15f), XMFLOAT3(0.4f, 1.0f, 0.9f), 2.0f, 30.0f, 0.32f);
}

void DirectXApp::UpdateCamera(float dt)
{
    const float moveSpeed = 5.f;

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));

    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

    XMVECTOR position = XMLoadFloat3(&mEyePos);

    if (GetAsyncKeyState('W') & 0x8000) {
        position += forward * moveSpeed * dt;
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        position -= forward * moveSpeed * dt;
    }
    if (GetAsyncKeyState('A') & 0x8000) {
        position -= right * moveSpeed * dt;
    }
    if (GetAsyncKeyState('D') & 0x8000) {
        position += right * moveSpeed * dt;
    }
    if (GetAsyncKeyState(VK_UP) & 0x8000) {
        position += worldUp * moveSpeed * dt;
    }
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        position -= worldUp * moveSpeed * dt;
    }

    XMStoreFloat3(&mEyePos, position);
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

    mRenderingSystem = std::make_unique<RenderingSystem>();
    if (!mRenderingSystem->Initialize(device.Get(), mClientWidth, mClientHeight, mBackBufferFormat, mDepthStencilFormat))
    {
        return false;
    }

    BuildScene();

    mTimer.Reset();
    return true;
}

bool DirectXApp::InitializeApp()
{
    return Initialize();
}

bool DirectXApp::CreateDXGIFactory()
{
    return SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));
}

bool DirectXApp::GetHardwareAdapter()
{
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(dxgiFactory.As(&factory6)))
    {
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
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)));

    device->CreateDepthStencilView(mDepthStencilBuffer.Get(), nullptr, DepthStencilView());

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

void DirectXApp::OnResize()
{
}

void DirectXApp::CalculateFrameStats()
{
    mFrameCount++;
    if ((mTimer.TotalTime() - mTimeElapsed) >= 1.0f)
    {
        float fps = static_cast<float>(mFrameCount);
        float mspf = 1000.0f / fps;

        std::wostringstream ws;
        ws << L"DirectX 12 Tessellation";
        ws << L" FPS: " << std::to_wstring(static_cast<int>(fps));
        ws << L" MSPF: " << std::to_wstring(mspf);
        SetWindowText(window.GetHandle(), ws.str().c_str());

        mFrameCount = 0;
        mTimeElapsed += 1.0f;
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

void DirectXApp::Update(const Timer& gt)
{
    const float dt = gt.DeltaTime();

    UpdateCamera(dt);

    const bool tDown = (GetAsyncKeyState('T') & 0x8000) != 0;
    const bool rDown = (GetAsyncKeyState('R') & 0x8000) != 0;
    const bool cDown = (GetAsyncKeyState('C') & 0x8000) != 0;
    const bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
    const bool digit1Down = (GetAsyncKeyState('1') & 0x8000) != 0;
    const bool digit2Down = (GetAsyncKeyState('2') & 0x8000) != 0;
    const bool digit3Down = (GetAsyncKeyState('3') & 0x8000) != 0;

    if (tDown && !mTWasDown) {
        mAnimateTextures = !mAnimateTextures;
        mTitleDirty = true;
    }
    mTWasDown = tDown;

    if (rDown && !mRWasDown) {
        mAnimateTextures = false;
        mTexAnimU = 0.0f;
        mTexAnimV = 0.0f;
        mTexScaleU = 1.0f;
        mTexScaleV = 1.0f;
        mTitleDirty = true;
    }
    mRWasDown = rDown;

    if (cDown && !mCWasDown) {
        mFrustumCullingEnabled = !mFrustumCullingEnabled;
        mTitleDirty = true;
    }
    mCWasDown = cDown;

    if (oDown && !mOWasDown) {
        mOctreeCullingEnabled = !mOctreeCullingEnabled;
        mTitleDirty = true;
    }
    mOWasDown = oDown;

    if (digit1Down && !mDigit1WasDown) {
        ActivateScene(0, true);
        mTitleDirty = true;
    }
    mDigit1WasDown = digit1Down;

    if (digit2Down && !mDigit2WasDown) {
        ActivateScene(1, true);
        mTitleDirty = true;
    }
    mDigit2WasDown = digit2Down;

    if (digit3Down && !mDigit3WasDown) {
        ActivateScene(2, true);
        mTitleDirty = true;
    }
    mDigit3WasDown = digit3Down;

    if (GetAsyncKeyState('Y') & 0x8000) { mTexScaleU += dt * 1.2f; mTexScaleV += dt * 1.2f; }
    if (GetAsyncKeyState('H') & 0x8000) { mTexScaleU -= dt * 1.2f; mTexScaleV -= dt * 1.2f; }
    if (GetAsyncKeyState('U') & 0x8000) { mTexScaleU += dt * 1.2f; }
    if (GetAsyncKeyState('J') & 0x8000) { mTexScaleU -= dt * 1.2f; }
    if (GetAsyncKeyState('I') & 0x8000) { mTexScaleV += dt * 1.2f; }
    if (GetAsyncKeyState('K') & 0x8000) { mTexScaleV -= dt * 1.2f; }

    mTexScaleU = std::clamp(mTexScaleU, 0.10f, 16.0f);
    mTexScaleV = std::clamp(mTexScaleV, 0.10f, 16.0f);

    if (mAnimateTextures) {
        mTexAnimU += 0.04f * dt;
        mTexAnimV += 0.015f * dt;
        if (mTexAnimU > 1.0f) mTexAnimU -= 1.0f;
        if (mTexAnimV > 1.0f) mTexAnimV -= 1.0f;
    }

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));

    const XMVECTOR eye = XMLoadFloat3(&mEyePos);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
                                                   static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
                                                   0.1f,
                                                   5000.0f);

    const XMMATRIX texTransform =
        XMMatrixScaling(mTexScaleU, mTexScaleV, 1.0f) *
        XMMatrixTranslation(mTexAnimU, mTexAnimV, 0.0f);

    CollectVisibleObjects();

    for (unsigned int objectIndex : mVisibleObjects) {
        const SceneObject& object = mSceneObjects[objectIndex];
        const XMMATRIX world = XMLoadFloat4x4(&object.world);

        ObjectConstants obj = {};
        XMStoreFloat4x4(&obj.WorldViewProj, XMMatrixTranspose(world * view * proj));
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&obj.TextureTransform, XMMatrixTranspose(texTransform));
        obj.TotalTime = gt.TotalTime();
        obj.Padding.x = static_cast<float>(mDebugViewMode);
        obj.Padding.y = 0.06f;
        obj.Padding.z = 0.0f;
        mObjectCB->CopyData(static_cast<int>(objectIndex), obj);
    }

    PassConstants pass = {};
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    XMStoreFloat4x4(&pass.InvViewProj, XMMatrixTranspose(invViewProj));
    pass.EyePosW = mEyePos;
    pass.AmbientColor = XMFLOAT4(0.08f, 0.08f, 0.1f, 1.0f);
    mPassCB->CopyData(0, pass);

    if (mTitleDirty || mDebugViewMode != mLastTitleMode) {
        UpdateWindowTitle();
        mLastTitleMode = mDebugViewMode;
        mTitleDirty = false;
    }
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

D3D12_GPU_DESCRIPTOR_HANDLE DirectXApp::GetGpuSrvHandle(unsigned int heapIndex) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(heapIndex) * mCbvSrvUavDescriptorSize;
    return handle;
}

void DirectXApp::Draw(const Timer& gt)
{
    if (mSceneMesh.submeshes.empty() || mSceneMesh.indices.empty() || mSceneMesh.vertices.empty()) {
        return;
    }

    FlushCommandQueue();

    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    ID3D12DescriptorHeap* descriptorHeaps[] = {mCbvHeap.Get()};
    mCommandList->SetDescriptorHeaps(1, descriptorHeaps);

    auto* gbuffer = mRenderingSystem->GetGBuffer();

    D3D12_RESOURCE_BARRIER toRT[3] = {
        CD3DX12_RESOURCE_BARRIER::Transition(gbuffer->GetTexture(GBuffer::GBUFFER_ALBEDO), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(gbuffer->GetTexture(GBuffer::GBUFFER_NORMAL), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(gbuffer->GetTexture(GBuffer::GBUFFER_DEPTH), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    mCommandList->ResourceBarrier(3, toRT);

    D3D12_CPU_DESCRIPTOR_HANDLE gbuffRtvs[3] = {
        gbuffer->GetRTV(GBuffer::GBUFFER_ALBEDO),
        gbuffer->GetRTV(GBuffer::GBUFFER_NORMAL),
        gbuffer->GetRTV(GBuffer::GBUFFER_DEPTH)
    };

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    gbuffer->ClearRenderTargets(mCommandList.Get());
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(3, gbuffRtvs, FALSE, &dsv);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetGeometryRootSignature());

    const unsigned int objectCbvStart = 0;
    const unsigned int passCbvIndex = objectCbvStart + (std::max)(1u, static_cast<unsigned int>(mSceneObjects.size()));
    mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrvHandle(passCbvIndex));

    for (unsigned int objectIndex : mVisibleObjects) {
        const SceneObject& object = mSceneObjects[objectIndex];
        const ModelAsset& model = mModelAssets[object.modelIndex];

        mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(objectCbvStart + objectIndex));

        for (unsigned int submeshOffset = 0; submeshOffset < model.submeshCount; ++submeshOffset) {
            const Submesh& submesh = mSceneMesh.submeshes[model.startIndex + submeshOffset];
            const bool hasDisplacement = !submesh.material.displacementTextureName.empty() &&
                                         submesh.material.displacementSrvHeapIndex !=
                                             mTextureResources[mFallbackDisplacementIndex].srvHeapIndex;
            const bool wireframeDebug = (mDebugViewMode == 3);

            if (hasDisplacement) {
                mCommandList->SetPipelineState(wireframeDebug
                                                   ? mRenderingSystem->GetTessellationWirePSO()
                                                   : mRenderingSystem->GetTessellationPSO());
                mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
            } else {
                mCommandList->SetPipelineState(wireframeDebug
                                                   ? mRenderingSystem->GetGeometryWirePSO()
                                                   : mRenderingSystem->GetGeometryPSO());
                mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrvHandle(submesh.material.diffuseSrvHeapIndex));
            mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrvHandle(submesh.material.normalSrvHeapIndex));
            mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrvHandle(submesh.material.displacementSrvHeapIndex));

            mCommandList->DrawIndexedInstanced(
                submesh.indexCount,
                1,
                submesh.startIndexLocation,
                submesh.baseVertexLocation,
                0);
        }
    }

    D3D12_RESOURCE_BARRIER toSrv[3] = {
        CD3DX12_RESOURCE_BARRIER::Transition(gbuffer->GetTexture(GBuffer::GBUFFER_ALBEDO), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(gbuffer->GetTexture(GBuffer::GBUFFER_NORMAL), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(gbuffer->GetTexture(GBuffer::GBUFFER_DEPTH), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    mCommandList->ResourceBarrier(3, toSrv);

    auto bbToRt = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &bbToRt);

    const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto rtv = CurrentBackBufferView();
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);

    mCommandList->SetPipelineState(mRenderingSystem->GetLightingPSO());
    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetLightingRootSignature());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(mGBufferSrvStart));
    mCommandList->SetGraphicsRootConstantBufferView(1, mPassCB->Resource()->GetGPUVirtualAddress());

    const unsigned int lightElementSize = mLightingCB->GetElementSize();
    unsigned int lightCbIndex = 0;
    const auto lightingCbAddress = [&](unsigned int index) -> D3D12_GPU_VIRTUAL_ADDRESS {
        return mLightingCB->Resource()->GetGPUVirtualAddress() + static_cast<UINT64>(index) * lightElementSize;
    };

    LightingConstants ambientConst = {};
    ambientConst.EnableAmbient = 1;
    mLightingCB->CopyData(static_cast<int>(lightCbIndex), ambientConst);
    mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
    mCommandList->DrawInstanced(3, 1, 0, 0);
    ++lightCbIndex;

    for (const auto& light : mLights) {
        if (lightCbIndex >= LightingCbElementCount) {
            break;
        }

        LightingConstants lightConst = {};
        lightConst.EnableAmbient = 0;
        lightConst.Light = light;
        mLightingCB->CopyData(static_cast<int>(lightCbIndex), lightConst);
        mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
        mCommandList->DrawInstanced(3, 1, 0, 0);
        ++lightCbIndex;
    }

    auto bbToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &bbToPresent);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    ThrowIfFailed(mSwapChain->Present(1, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

void DirectXApp::Shutdown()
{
    FlushCommandQueue();

    if (mRenderingSystem)
    {
        mRenderingSystem.reset();
    }

    mPassCB.reset();
    mObjectCB.reset();
    mLightingCB.reset();

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
