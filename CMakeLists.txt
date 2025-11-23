cmake_minimum_required(VERSION 3.21)

project(
	FrameGeneration
	VERSION 1.1.5
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)

find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)


target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
	${SIMPLEINI_INCLUDE_DIRS}
)


target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	debug ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Debug/detours.lib
	optimized ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Release/detours.lib
	Microsoft::DirectXTK
	Microsoft::DirectXTex
	Microsoft::DirectX-Headers
	d3d12.lib
	magic_enum::magic_enum
	d3dcompiler.lib
)