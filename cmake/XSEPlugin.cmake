set(AIO ON CACHE BOOL "Build the unified NuclearGFX.dll (all-in-one)")
set(FRAMEGEN ON CACHE BOOL "Build the FrameGen plugin target")
set(REFLEX ON CACHE BOOL "Build the Reflex plugin target")
set(UPSCALER ON CACHE BOOL "Build the Upscaler plugin target")
set(OVERLAY ON CACHE BOOL "Build the Overlay plugin target")
set(COMMUNITY_SHADERS ON CACHE BOOL "Build the Community Shaders plugin target")

set(FO4CS_BUILD_AIO ${AIO} CACHE BOOL "Build the unified NuclearGFX.dll" FORCE)
set(FO4CS_BUILD_FRAMEGEN ${FRAMEGEN} CACHE BOOL "Build the FrameGen plugin target" FORCE)
set(FO4CS_BUILD_REFLEX ${REFLEX} CACHE BOOL "Build the Reflex plugin target" FORCE)
set(FO4CS_BUILD_UPSCALER ${UPSCALER} CACHE BOOL "Build the Upscaler plugin target" FORCE)
set(FO4CS_BUILD_OVERLAY ${OVERLAY} CACHE BOOL "Build the Overlay plugin target" FORCE)
set(FO4CS_BUILD_COMMUNITY_SHADERS ${COMMUNITY_SHADERS} CACHE BOOL "Build the Community Shaders plugin target" FORCE)

function(fo4cs_validate_plugin_selection)
    if(NOT AIO AND NOT FRAMEGEN AND NOT REFLEX AND NOT UPSCALER AND NOT COMMUNITY_SHADERS AND NOT OVERLAY)
        message(FATAL_ERROR "At least one plugin target must be enabled. Set AIO, FRAMEGEN, REFLEX, UPSCALER, COMMUNITY_SHADERS, and/or OVERLAY to ON.")
    endif()
endfunction()

# Single copy of the MSVC flag set shared by every target.
# /sdl- /GS- /guard:cf- are deliberate: mod plugins are local-run executables
# where the CFG guard overhead on the /O2 /arch:AVX hot paths is not worth it.
# Keep them off unless a security requirement changes that tradeoff.
function(fo4cs_apply_msvc_compile_options target)
    if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
        return()
    endif()

    set(_fo4cs_release_opts "/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")

    target_compile_options("${target}" PRIVATE
        /MP /W4 /WX /permissive-
        /Zc:alignedNew /Zc:auto /Zc:__cplusplus /Zc:externC /Zc:externConstexpr
        /Zc:forScope /Zc:hiddenFriend /Zc:implicitNoexcept /Zc:lambda
        /Zc:noexceptTypes /Zc:preprocessor /Zc:referenceBinding /Zc:rvalueCast
        /Zc:sizedDealloc /Zc:strictStrings /Zc:ternary /Zc:threadSafeInit
        /Zc:trigraphs /Zc:wchar_t /wd4200 /arch:AVX
    )

    target_compile_options("${target}" PUBLIC
        "$<$<CONFIG:DEBUG>:/fp:strict>" "$<$<CONFIG:DEBUG>:/ZI>"
        "$<$<CONFIG:DEBUG>:/Od>" "$<$<CONFIG:DEBUG>:/Gy>"
        "$<$<CONFIG:RELEASE>:${_fo4cs_release_opts}>"
    )
endfunction()

# imgui sources are compiled without the PCH and with warnings suppressed;
# the vendored code does not follow the project's /W4 /WX discipline.
function(fo4cs_add_imgui_sources target)
    set_source_files_properties(${ARGN} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "/W0")
    endif()
endfunction()

function(xseplugin_resolve_commonlib_root out_root out_name)
    if(BUILD_PRE_NG)
        add_compile_definitions(FALLOUT_PRE_NG)
        set(_commonlib_name "CommonLibF4")
        set(_commonlib_root "extern/CommonLibF4PreNG")
        set(GamePath ${Fallout4VRPath})
    elseif(BUILD_POST_NG)
        add_compile_definitions(FALLOUT_POST_NG)
        set(_commonlib_name "CommonLibF4")
        set(_commonlib_root "extern/CommonLibF4PostNG")
    elseif(BUILD_POST_AE)
        add_compile_definitions(FALLOUT_POST_NG)
        add_compile_definitions(FALLOUT_POST_AE)
        set(_commonlib_name "CommonLibF4")
        set(_commonlib_root "extern/CommonLibF4PostAE")
    else()
        message(FATAL_ERROR "No CommonLibF4 variant selected. Enable one of BUILD_PRE_NG, BUILD_POST_NG, or BUILD_POST_AE.")
    endif()

    set(${out_root} "${_commonlib_root}" PARENT_SCOPE)
    set(${out_name} "${_commonlib_name}" PARENT_SCOPE)
endfunction()

function(xseplugin_resolve_commonlib_project out_path commonlib_root)
    unset(_resolved_path)
    foreach(_commonlib_candidate
        "${commonlib_root}/CommonLibF4"
        "${commonlib_root}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_commonlib_candidate}/CMakeLists.txt")
            set(_resolved_path "${_commonlib_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _resolved_path)
        message(FATAL_ERROR "Could not locate a CommonLibF4 CMake project under ${commonlib_root}")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_resolved_path}/.gitmodules"
        AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_resolved_path}/lib/commonlib-shared/CMakeLists.txt")
        message(FATAL_ERROR
            "CommonLibF4 at ${_resolved_path} is missing nested submodules. "
            "Run: git submodule update --init --recursive ${commonlib_root}")
    endif()

    set(${out_path} "${_resolved_path}" PARENT_SCOPE)
endfunction()

function(xseplugin_resolve_commonlib_target out_target)
    if(TARGET CommonLibF4::CommonLibF4)
        set(${out_target} "CommonLibF4::CommonLibF4" PARENT_SCOPE)
    elseif(TARGET CommonLibF4::commonlibf4)
        set(${out_target} "CommonLibF4::commonlibf4" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "CommonLibF4 target not found after adding subdirectory.")
    endif()
endfunction()

function(fo4cs_apply_plugin_defaults target)
    target_compile_features(
        "${target}"
        PRIVATE
        cxx_std_23
    )

    target_precompile_headers(
        "${target}"
        PRIVATE
        include/PCH.h
    )

    target_include_directories(
        "${target}"
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${FO4CS_GENERATED_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(WIN32)
        target_compile_definitions("${target}" PRIVATE _WINDOWS)
    endif()

    target_compile_definitions("${target}" PRIVATE _AMD64_)

    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_compile_definitions("${target}" PRIVATE _UNICODE "$<$<CONFIG:DEBUG>:DEBUG>")

        fo4cs_apply_msvc_compile_options("${target}")

        target_link_options(
            "${target}"
            PRIVATE
            /WX
            "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
            "$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
        )
    endif()
endfunction()

function(fo4cs_configure_plugin_metadata target plugin_name plugin_display_name)
    set(FO4CS_PLUGIN_NAME "${plugin_name}")
    set(FO4CS_PLUGIN_DISPLAY_NAME "${plugin_display_name}")
    set(_plugin_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/cmake/${target}")
    file(MAKE_DIRECTORY "${_plugin_generated_dir}")

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
        ${_plugin_generated_dir}/Plugin.h
        @ONLY
    )

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.rc.in
        ${_plugin_generated_dir}/version.rc
        @ONLY
    )

    target_sources(
        "${target}"
        PRIVATE
        ${_plugin_generated_dir}/Plugin.h
        ${_plugin_generated_dir}/version.rc
    )
endfunction()

function(fo4cs_create_plugin target plugin_name plugin_display_name)
    add_library("${target}" SHARED)
    fo4cs_apply_plugin_defaults("${target}")
    fo4cs_configure_plugin_metadata("${target}" "${plugin_name}" "${plugin_display_name}")

    target_link_libraries(
        "${target}"
        PUBLIC
        ${ARGN}
    )
endfunction()
