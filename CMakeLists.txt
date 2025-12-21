cmake_minimum_required(VERSION 3.21)

project(
	MotionVectorFixes
	VERSION 1.0.4
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	debug ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Debug/detours.lib
	optimized ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Release/detours.lib
)