cmake_minimum_required(VERSION 3.21)

project(
    fo4CS
    VERSION 1.3.4
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)

find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

fo4cs_validate_plugin_selection()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

xseplugin_resolve_commonlib_root(CommonLibRoot CommonLibName)
xseplugin_resolve_commonlib_project(CommonLibPath "${CommonLibRoot}")

# FSR runtime: all game runtimes use amd_fidelityfx_dx12.dll through the D3D12 proxy.

if(F4SE_SUPPORT_XBYAK)
  set(COMMONLIBF4_ENABLE_XBYAK ON CACHE BOOL "Enable xbyak support for CommonLibF4" FORCE)
endif()

add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)
xseplugin_resolve_commonlib_target(CommonLibTarget)

set(FO4CS_COMMON_DEPENDENCIES
    ${CommonLibTarget}
    spdlog::spdlog
    debug ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Debug/detours.lib
    optimized ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Release/detours.lib
    Microsoft::DirectXTK
    Microsoft::DirectXTex
    Microsoft::DirectX-Headers
    d3d12.lib
    dxgi.lib
    advapi32.lib
    magic_enum::magic_enum
    d3dcompiler.lib
)

set(FEATURE_VERSIONS
    "		{\"LightLimitFix\"sv, {0,1,0}}"
    "		{\"ExtendedMaterials\"sv, {0,1,0}}"
    "		{\"ShaderDump\"sv, {0,1,0}}"
    "		{\"Upscaling\"sv, {1,0,0}}"
    "		{\"FrameGeneration\"sv, {1,0,0}}"
    "		{\"Reflex\"sv, {1,0,0}}"
    "		{\"Overlay\"sv, {1,0,0}}"
)
string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")
set(FO4CS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FeatureVersions.h.in
    ${FO4CS_GENERATED_INCLUDE_DIR}/Core/FeatureVersions.h
    @ONLY
)

add_library(Core OBJECT
    src/Buffer.h
    src/DX11Hooks.cpp
    src/DX11Hooks.h
    src/DX12SwapChain.cpp
    src/DX12SwapChain.h
    src/FidelityFX.cpp
    src/FidelityFX.h
    src/PresentationMenuPolicy.h
    src/RuntimeAdapter.cpp
    src/RuntimeAdapter.h
    src/Streamline.cpp
    src/Streamline.h
    src/Upscaler.cpp
    src/Upscaler.h
    src/UpscalerRenderBackend.cpp
)

target_compile_features(Core PRIVATE cxx_std_23)

target_include_directories(Core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
  target_compile_definitions(Core PRIVATE _WINDOWS)
endif()
target_compile_definitions(Core PRIVATE
    _AMD64_
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:FO4CS_ENABLE_DEBUG_SETTINGS=1>"
)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
  target_compile_definitions(Core PRIVATE _UNICODE "$<$<CONFIG:DEBUG>:DEBUG>")

  fo4cs_apply_msvc_compile_options(Core)
endif()

target_precompile_headers(Core PRIVATE include/PCH.h)
target_link_libraries(Core PUBLIC ${FO4CS_COMMON_DEPENDENCIES})

# ImGui core sources in Core — shared by all consumers for panel rendering.
# Feature DLLs call ImGui functions in their registered panel callbacks.
# Overlay.dll owns the D3D12 backend + context.
set(FO4CS_IMGUI_CORE_SOURCES
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)
target_sources(Core PRIVATE ${FO4CS_IMGUI_CORE_SOURCES})
fo4cs_add_imgui_sources(Core ${FO4CS_IMGUI_CORE_SOURCES})

if(COMMUNITY_SHADERS)

  add_library(CoreCS OBJECT
    src/Core/BSShaderHooks.cpp
    src/Core/BSShaderHooks.h
    src/Core/CommunityShaders.cpp
    src/Core/CommunityShaders.h
    src/Core/Deferred.cpp
    src/Core/Deferred.h
    src/Core/D3D11DeviceHooks.cpp
    src/Core/DiagnosticsFormatter.cpp
    src/Core/DiagnosticsFormatter.h
    src/Core/Feature.cpp
    src/Core/Feature.h
    src/Core/FeatureBuffer.cpp
    src/Core/FeatureBuffer.h
    src/Core/FeatureCategories.h
    src/Core/FeatureConstraints.cpp
    src/Core/FeatureConstraints.h
    src/Core/FeatureIssues.h
    src/Core/Globals.cpp
    src/Core/Globals.h
    src/Core/AdditivePasses.cpp
    src/Core/Hooks.cpp
    src/Core/Hooks.h
    src/Core/HooksInternal.h
    src/Core/IMenuItem.h
    src/Core/LLFPixelTracker.cpp
    src/Core/Menu.cpp
    src/Core/Menu.h
    src/Core/MenuRegistry.h
    src/Core/ShaderCache.cpp
    src/Core/ShaderCache.h
    src/Core/ShaderDB.cpp
    src/Core/ShaderDB.h
    src/Core/ShaderCompiler.cpp
    src/Core/ShaderCompiler.h
    src/Core/State.cpp
    src/Core/State.h
    src/Features/ExtendedMaterials.cpp
    src/Features/ExtendedMaterials.h
    src/Features/ShaderDump.cpp
    src/Features/ShaderDump.h
    src/Features/LightLimitFix.cpp
    src/Features/LightLimitFix.h
)

  if(COMMUNITY_SHADERS)
    target_sources(CoreCS PRIVATE
        src/Features/Upscaling.cpp
        src/Features/FrameGeneration.cpp
        src/Features/Reflex.cpp
        src/Features/Overlay.cpp
        src/Overlay/Overlay.cpp src/Overlay/Overlay.h
    )
    target_include_directories(CoreCS PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include
    )
  endif()

  target_compile_features(CoreCS PRIVATE cxx_std_23)

  target_include_directories(CoreCS
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${FO4CS_GENERATED_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

  if(WIN32)
    target_compile_definitions(CoreCS PRIVATE _WINDOWS)
  endif()
  target_compile_definitions(CoreCS PRIVATE
    _AMD64_
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:FO4CS_ENABLE_DEBUG_SETTINGS=1>"
)

  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    target_compile_definitions(CoreCS PRIVATE _UNICODE "$<$<CONFIG:DEBUG>:DEBUG>")

    fo4cs_apply_msvc_compile_options(CoreCS)
  endif()

  target_precompile_headers(CoreCS PRIVATE include/PCH.h)
  target_link_libraries(CoreCS PUBLIC ${FO4CS_COMMON_DEPENDENCIES})
endif()

if(BUILD_PRE_NG)
  set(FO4CS_RUNTIME_FLAVOR "PreNG")
elseif(BUILD_POST_NG)
  set(FO4CS_RUNTIME_FLAVOR "PostNG")
elseif(BUILD_POST_AE)
  set(FO4CS_RUNTIME_FLAVOR "PostAE")
else()
  set(FO4CS_RUNTIME_FLAVOR "Unknown")
endif()

# ------------------------------------------------------------------
# 插件目标
# ------------------------------------------------------------------
if(AIO)
  set(FRAMEGEN OFF)
  set(REFLEX OFF)
  set(UPSCALER OFF)
endif()

# community-shaders 分支: 全进 CommunityShaders.dll
if(COMMUNITY_SHADERS)
  set(AIO OFF)
  set(FRAMEGEN OFF)
  set(REFLEX OFF)
  set(UPSCALER OFF)
  set(OVERLAY OFF)
endif()

if(FRAMEGEN)
  fo4cs_create_plugin(FrameGen FrameGen "FrameGen" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(FrameGen PRIVATE
        src/FrameGenPlugin.cpp
    )
  target_compile_definitions(FrameGen PRIVATE FRAMEGEN=1)
  target_link_libraries(FrameGen PRIVATE Core)
endif()

if(REFLEX)
  fo4cs_create_plugin(Reflex Reflex "Reflex" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(Reflex PRIVATE
        src/ReflexPlugin.cpp
    )
  target_compile_definitions(Reflex PRIVATE REFLEX=1)
  target_link_libraries(Reflex PRIVATE Core)
endif()

if(UPSCALER)
  fo4cs_create_plugin(Upscaler Upscaler "Upscaler" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(Upscaler PRIVATE
        src/UpscalerPlugin.cpp
    )
  target_compile_definitions(Upscaler PRIVATE
        UPSCALER=1
        UPSCALER_LDR=1
    )
  target_link_libraries(Upscaler PRIVATE Core)
endif()

if(AIO)
  fo4cs_create_plugin(NuclearGFX NuclearGFX "NuclearGFX" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(NuclearGFX PRIVATE
        src/AIOPlugin.cpp
        src/Overlay/Overlay.cpp
        extern/imgui/backends/imgui_impl_dx11.cpp
     extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  target_include_directories(NuclearGFX PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    )
  fo4cs_add_imgui_sources(NuclearGFX
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  set_target_properties(NuclearGFX PROPERTIES OUTPUT_NAME "NuclearGFX")
  target_link_libraries(NuclearGFX PRIVATE Core)
endif()

if(COMMUNITY_SHADERS)
  fo4cs_create_plugin(CommunityShaders CommunityShaders "CommunityShaders" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(CommunityShaders PRIVATE
        src/CommunityShadersPlugin.cpp
        src/Presentation/PresentationBackend.cpp
        src/Presentation/PresentationBackend.h
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
    )
  fo4cs_add_imgui_sources(CommunityShaders
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  target_compile_definitions(CommunityShaders PRIVATE COMMUNITY_SHADERS)
  target_link_libraries(CommunityShaders PRIVATE Core CoreCS)
endif()
if(OVERLAY)
  fo4cs_create_plugin(Overlay Overlay "Overlay" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(Overlay PRIVATE
        src/OverlayPlugin.cpp
        src/Overlay/Overlay.cpp
        src/Overlay/Overlay.h
        src/Overlay/OverlayWndProc.h
    )
  target_include_directories(Overlay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    )
  target_link_libraries(Overlay PRIVATE Core)

  # D3D12/Win32 ImGui backends — only Overlay.dll needs these
  target_sources(Overlay PRIVATE
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  fo4cs_add_imgui_sources(Overlay
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
endif()
