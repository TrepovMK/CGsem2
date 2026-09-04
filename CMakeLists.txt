cmake_minimum_required(VERSION 3.20)
project(CG_Sem2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(assimp REQUIRED)

set(SRC_FILES
    src/main.cpp
    src/Window.cpp
    src/DirectXApp.cpp
    src/RenderingSystem.cpp
    src/GBuffer.cpp
    src/model_loader.cpp
    src/DDSTextureLoader.cpp
    src/d3dUtil.cpp
    src/Timer.cpp
    src/InputDevice.cpp
)

set(HEADER_FILES
    h/Window.h
    h/DirectXApp.h
    h/RenderingSystem.h
    h/GBuffer.h
    h/model_loader.h
    h/mesh_data.h
    h/DDSTextureLoader.h
    h/d3dUtil.h
    h/UploadBuffer.h
    h/Timer.h
    h/Vertex.h
    h/Material.h
    h/Light.h
    h/d3dx12.h
    h/ObjectConstants.h
    h/CameraConstants.h
    h/Submesh.h
    h/MathHelper.h
    h/ThrowIfFailed.h
    h/InputDevice.h
)

set(SHADER_FILES
    src/shaders.hlsl
    src/lighting.hlsl
)

add_executable(CG_Sem2
    ${SRC_FILES}
    ${HEADER_FILES}
    ${SHADER_FILES}
)

target_include_directories(CG_Sem2 PRIVATE
    ${CMAKE_SOURCE_DIR}/h
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(CG_Sem2 PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(CG_Sem2 PRIVATE
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    windowscodecs.lib
    assimp::assimp
)

if(MSVC)
    target_compile_options(CG_Sem2 PRIVATE /W4 /EHsc)
endif()
