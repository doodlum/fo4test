# Resolve the current Visual Studio MSVC toolset before project() enables C/C++.
# This keeps CMake Tools independent of a pre-populated Developer Command Prompt.
if(NOT WIN32)
    message(FATAL_ERROR "fo4CS requires the Windows MSVC toolchain.")
endif()

set(_fo4cs_vswhere "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
if(NOT EXISTS "${_fo4cs_vswhere}")
    message(FATAL_ERROR "fo4CS could not find vswhere.exe at '${_fo4cs_vswhere}'.")
endif()

execute_process(
    COMMAND "${_fo4cs_vswhere}" -latest -products *
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
        -property installationPath
    OUTPUT_VARIABLE _fo4cs_vs_root
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _fo4cs_vswhere_result
)
if(NOT _fo4cs_vswhere_result EQUAL 0 OR NOT _fo4cs_vs_root)
    message(FATAL_ERROR "fo4CS could not locate a Visual Studio installation with MSVC C++ tools.")
endif()

if(NOT DEFINED CMAKE_C_COMPILER OR NOT EXISTS "${CMAKE_C_COMPILER}"
   OR NOT DEFINED CMAKE_CXX_COMPILER OR NOT EXISTS "${CMAKE_CXX_COMPILER}")
    file(GLOB _fo4cs_msvc_toolsets LIST_DIRECTORIES true "${_fo4cs_vs_root}/VC/Tools/MSVC/*")
    list(SORT _fo4cs_msvc_toolsets COMPARE NATURAL ORDER DESCENDING)

    foreach(_fo4cs_msvc_toolset IN LISTS _fo4cs_msvc_toolsets)
        set(_fo4cs_cl "${_fo4cs_msvc_toolset}/bin/Hostx64/x64/cl.exe")
        if(EXISTS "${_fo4cs_cl}")
            set(CMAKE_C_COMPILER "${_fo4cs_cl}" CACHE FILEPATH "MSVC C compiler" FORCE)
            set(CMAKE_CXX_COMPILER "${_fo4cs_cl}" CACHE FILEPATH "MSVC C++ compiler" FORCE)
            message(STATUS "fo4CS selected MSVC compiler: ${_fo4cs_cl}")
            break()
        endif()
    endforeach()

    if(NOT DEFINED CMAKE_C_COMPILER OR NOT EXISTS "${CMAKE_C_COMPILER}"
       OR NOT DEFINED CMAKE_CXX_COMPILER OR NOT EXISTS "${CMAKE_CXX_COMPILER}")
        message(FATAL_ERROR "fo4CS found Visual Studio at '${_fo4cs_vs_root}' but no x64 MSVC compiler.")
    endif()
endif()

set(_fo4cs_vcpkg_toolchain "${_fo4cs_vs_root}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
if(NOT EXISTS "${_fo4cs_vcpkg_toolchain}")
    message(FATAL_ERROR "fo4CS could not find the Visual Studio vcpkg toolchain at '${_fo4cs_vcpkg_toolchain}'.")
endif()

include("${_fo4cs_vcpkg_toolchain}")
