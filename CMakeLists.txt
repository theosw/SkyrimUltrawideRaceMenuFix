cmake_minimum_required(VERSION 3.21)

project(
	UltrawideChargenCameraFix
	VERSION 0.1.1
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

add_compile_definitions(SKYRIM)

add_library("${PROJECT_NAME}" SHARED)

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(BUILD_TESTS OFF)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)

include(AddCXXFiles)
add_cxx_files("${PROJECT_NAME}")

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Version.rc
	@ONLY
)

target_sources(
	"${PROJECT_NAME}"
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Version.rc
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE include/PCH.h)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	add_compile_definitions(_UNICODE)
	target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")

	target_compile_options(
		"${PROJECT_NAME}"
		PRIVATE
		/MP
		/W4
		/permissive-
		/Zc:__cplusplus
		/arch:AVX
	)

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

# Shared local checkout: by default reuse the FrameGenTest workspace's
# CommonLibSSE-NG so this builds against the same dependency as FableFG.
# Override with -D to make this repo standalone.
set(
	UCCF_COMMONLIBSSE_NG_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/../FrameGenTest/extern/CommonLibSSE-NG"
	CACHE PATH
	"Path to a CommonLibSSE-NG checkout"
)

if(NOT EXISTS "${UCCF_COMMONLIBSSE_NG_DIR}/CMakeLists.txt")
	message(
		FATAL_ERROR
		"CommonLibSSE-NG was not found at '${UCCF_COMMONLIBSSE_NG_DIR}'. "
		"Clone it there or configure with -D UCCF_COMMONLIBSSE_NG_DIR=<path>."
	)
endif()

add_subdirectory("${UCCF_COMMONLIBSSE_NG_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)

target_include_directories(
	${PROJECT_NAME}
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PUBLIC
	CommonLibSSE::CommonLibSSE
)

set(UCCF_PACKAGE_PLUGINS "${CMAKE_CURRENT_SOURCE_DIR}/package/SKSE/Plugins")
set(UCCF_INI "${UCCF_PACKAGE_PLUGINS}/UltrawideChargenCameraFix.ini")

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
	string(STRIP "$ENV{SKYRIM_MODS_FOLDER}" SKYRIM_MODS_FOLDER_STRIPPED)
	list(APPEND UCCF_OUTPUT_FOLDERS "${SKYRIM_MODS_FOLDER_STRIPPED}/UltrawideChargenCameraFix")
endif()
# "[No Delete]" keeps the mod safe across Wabbajack list updates. Deploys to
# every install found (LoreRim 5.x at D:/Lorerim, legacy 4.x install). Both
# installs run the AE runtime, so only the AE build may deploy — an SE build
# deploying here would ship a DLL the game refuses to load.
if(ENABLE_SKYRIM_AE)
	foreach(_mods_dir "D:/Lorerim/mods" "D:/Games/US SSE/Lorerim/game-files/mods")
		if(IS_DIRECTORY "${_mods_dir}")
			list(APPEND UCCF_OUTPUT_FOLDERS "${_mods_dir}/[No Delete] Ultrawide Chargen Camera Fix")
		endif()
	endforeach()
endif()

foreach(UCCF_OUTPUT_FOLDER IN LISTS UCCF_OUTPUT_FOLDERS)
	set(UCCF_DEPLOY_PLUGIN_FOLDER "${UCCF_OUTPUT_FOLDER}/SKSE/Plugins")
	message(STATUS "UltrawideChargenCameraFix deploy folder: ${UCCF_DEPLOY_PLUGIN_FOLDER}")

	add_custom_command(
		TARGET "${PROJECT_NAME}"
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${UCCF_DEPLOY_PLUGIN_FOLDER}"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${UCCF_DEPLOY_PLUGIN_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${UCCF_DEPLOY_PLUGIN_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
		COMMAND "${CMAKE_COMMAND}" -DSRC=${UCCF_INI} -DDST=${UCCF_DEPLOY_PLUGIN_FOLDER}/UltrawideChargenCameraFix.ini -DDEFAULT_DST=${UCCF_DEPLOY_PLUGIN_FOLDER}/UltrawideChargenCameraFix.default.ini -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployIniIfMissing.cmake"
		VERBATIM
	)
endforeach()
