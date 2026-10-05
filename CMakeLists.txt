cmake_minimum_required(VERSION 3.16)
project(RwAssetLib LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(RWASSETLIB_TOOLS_DEFAULT ON)
else()
    set(RWASSETLIB_TOOLS_DEFAULT OFF)
endif()

option(RWASSETLIB_BUILD_TOOLS "Build the rwasset command-line tool" ${RWASSETLIB_TOOLS_DEFAULT})
option(RWASSETLIB_BUILD_TESTS "Build the RwAssetLib test suite" OFF)

set(CORE_SOURCES
    src/core/RwStream.cpp
    src/core/RwChunk.cpp
    src/core/DffError.cpp
)
set(CORE_HEADERS
    src/core/RwTypes.h
    src/core/RwStream.h
    src/core/RwChunk.h
    src/core/DffError.h
)

set(WORLD_SOURCES
    src/world/Extension.cpp
    src/world/Texture.cpp
    src/world/Material.cpp
    src/world/Geometry.cpp
    src/world/Clump.cpp
    src/world/CameraLight.cpp
)
set(WORLD_HEADERS
    src/world/Extension.h
    src/world/Texture.h
    src/world/Material.h
    src/world/Geometry.h
    src/world/Clump.h
    src/world/CameraLight.h
)

set(PLUGIN_SOURCES
    src/plugin/UserData.cpp
    src/plugin/HAnim.cpp
    src/plugin/Skin.cpp
    src/plugin/BinMesh.cpp
    src/plugin/Toon.cpp
    src/plugin/Morph.cpp
    src/plugin/Collis.cpp
    src/plugin/MatFX.cpp
    src/plugin/UVAnim.cpp
    src/plugin/LtMap.cpp
)
set(PLUGIN_HEADERS
    src/plugin/UserData.h
    src/plugin/HAnim.h
    src/plugin/Skin.h
    src/plugin/BinMesh.h
    src/plugin/Toon.h
    src/plugin/Morph.h
    src/plugin/Collis.h
    src/plugin/MatFX.h
    src/plugin/UVAnim.h
    src/plugin/LtMap.h
)

set(NTL_SOURCES
    src/ntl/NtlMaterial.cpp
    src/ntl/NtlAtomic.cpp
)
set(NTL_HEADERS
    src/ntl/NtlMaterial.h
    src/ntl/NtlAtomic.h
)

set(ANIM_SOURCES  src/anim/AnmDocument.cpp
                 src/anim/UvaDocument.cpp)
set(ANIM_HEADERS  src/anim/AnmDocument.h
                 src/anim/UvaDocument.h)

set(DFF_SOURCES   src/dff/DffDocument.cpp)
set(DFF_HEADERS   src/dff/DffDocument.h)

set(BUILD_SOURCES
    src/build/RwTriStrip.cpp
    src/build/BinMeshBuilder.cpp
    src/build/ToonBuilder.cpp
    src/build/SkinBuilder.cpp
    src/build/CollisBuilder.cpp
    src/build/BoundsBuilder.cpp
)
set(BUILD_HEADERS
    src/build/RwTriStrip.h
    src/build/BinMeshBuilder.h
    src/build/ToonBuilder.h
    src/build/SkinBuilder.h
    src/build/CollisBuilder.h
    src/build/BoundsBuilder.h
)

source_group("core"   FILES ${CORE_SOURCES} ${CORE_HEADERS})
source_group("world"  FILES ${WORLD_SOURCES} ${WORLD_HEADERS})
source_group("plugin" FILES ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})
source_group("ntl"    FILES ${NTL_SOURCES} ${NTL_HEADERS})
source_group("anim"   FILES ${ANIM_SOURCES} ${ANIM_HEADERS})
source_group("dff"    FILES ${DFF_SOURCES} ${DFF_HEADERS})
source_group("build"  FILES ${BUILD_SOURCES} ${BUILD_HEADERS})

add_library(RwAssetLib STATIC
    ${CORE_SOURCES}   ${CORE_HEADERS}
    ${WORLD_SOURCES}  ${WORLD_HEADERS}
    ${PLUGIN_SOURCES} ${PLUGIN_HEADERS}
    ${NTL_SOURCES}    ${NTL_HEADERS}
    ${ANIM_SOURCES}   ${ANIM_HEADERS}
    ${DFF_SOURCES}    ${DFF_HEADERS}
    ${BUILD_SOURCES}  ${BUILD_HEADERS}
)

target_include_directories(RwAssetLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(RwAssetLib PUBLIC cxx_std_17)
set_target_properties(RwAssetLib PROPERTIES
    CXX_EXTENSIONS OFF
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
)

if(MSVC)
    target_compile_definitions(RwAssetLib PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(RwAssetLib PRIVATE /W4 /wd4100)
endif()

if(RWASSETLIB_BUILD_TOOLS)
    add_executable(rwasset
        src/tools/main.cpp
        src/tools/RegenCheck.cpp
        src/tools/SkinCheck.cpp
        src/tools/CompareDff.cpp
    )
    target_link_libraries(rwasset PRIVATE RwAssetLib)
    set_target_properties(rwasset PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
    install(TARGETS rwasset RUNTIME DESTINATION bin)
endif()

if(RWASSETLIB_BUILD_TESTS)
    enable_testing()

    add_executable(RwAssetTests
        tests/tests_main.cpp
        tests/test_core_stream.cpp
        tests/test_chunk_span.cpp
        tests/test_dff_synthetic_roundtrip.cpp
        tests/test_dff_edit_roundtrip.cpp
        tests/test_anm_roundtrip.cpp
        tests/test_uva_roundtrip.cpp
        tests/test_builders.cpp
        tests/test_generated_chunks.cpp
        tests/test_plugin_formats.cpp
        tests/test_corpus_roundtrip.cpp
    )
    target_link_libraries(RwAssetTests PRIVATE RwAssetLib)
    target_include_directories(RwAssetTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    set_target_properties(RwAssetTests PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
    add_test(NAME RwAssetTests COMMAND RwAssetTests)
endif()
