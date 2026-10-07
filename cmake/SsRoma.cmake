# Everything RoMa v2, in two halves that SsNn.cmake and SsSfm.cmake know nothing
# about (docs/notes/densify.md).
#   host stage (SS_BUILD_SFM)  src/roma/*.cpp on ss_sfm: ss_roma_host and
#       roma_*_test, one executable per src/roma/tests/*.cpp.
#   network (SS_BUILD_SAM)     src/roma/model/ on the inference layer: ss_roma,
#       its tests (src/roma/model/tests/*.cpp) and tools/roma/roma_match_pairs.
# `spirula densify` needs both; its sources reach the app through the
# SS_EXT_TOOL_* variables SsApps.cmake reads.

if(SS_BUILD_SAM)
    # Included after SsNn.cmake, which defines ss_nn_shaders() and ss_nn.
    # The multi-view transformer's RoPE is the one shader, in bf16 by construction.
    ss_nn_shaders(roma ${SS_SRC}/roma/shaders SS_ROMA_EMBED)
    file(GLOB SS_ROMA_SOURCES CONFIGURE_DEPENDS ${SS_SRC}/roma/model/*.cpp)

    add_library(ss_roma STATIC ${SS_ROMA_SOURCES} ${SS_ROMA_EMBED})
    target_link_libraries(ss_roma PUBLIC ss_nn ss_license)
    target_compile_options(ss_roma PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${SPLAT_CXX_FLAGS}>)
    set_property(TARGET ss_roma PROPERTY CXX_STANDARD 17)

    file(GLOB SS_ROMA_MODEL_TESTS CONFIGURE_DEPENDS ${SS_SRC}/roma/model/tests/*.cpp)
    foreach(test_src ${SS_ROMA_MODEL_TESTS})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE ss_roma)
        set_property(TARGET ${test_name} PROPERTY CXX_STANDARD 17)
        target_compile_definitions(${test_name} PRIVATE SS_REPO_ROOT="${SS_ROOT}")
        target_compile_options(${test_name} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:${SPLAT_CXX_FLAGS}>)
    endforeach()

    # The licence test drives the terminal prompt and reads the translated wording.
    target_sources(roma_license_test PRIVATE ${SS_SRC}/app/cli/LicenseCli.cpp)
    target_link_libraries(roma_license_test PRIVATE ss_license ss_i18n)
endif()

if(NOT SS_BUILD_SFM)
    return()
endif()

file(GLOB SS_ROMA_HOST_SOURCES CONFIGURE_DEPENDS ${SS_SRC}/roma/*.cpp)

# roma::modelSourceDigest(), regenerated whenever a model or nn source changes.
file(GLOB_RECURSE SS_ROMA_DIGEST_INPUTS CONFIGURE_DEPENDS
     ${SS_SRC}/nn/*.cpp ${SS_SRC}/nn/*.h ${SS_SRC}/nn/*.slang
     ${SS_SRC}/roma/model/*.cpp ${SS_SRC}/roma/model/*.h ${SS_SRC}/roma/shaders/*
     ${SS_SRC}/roma/Roma.h ${SS_SRC}/roma/Common.h)
list(FILTER SS_ROMA_DIGEST_INPUTS EXCLUDE REGEX "/tests/")
set(SS_ROMA_DIGEST_CPP ${CMAKE_BINARY_DIR}/gen/roma/ModelDigest.cpp)
add_custom_command(OUTPUT ${SS_ROMA_DIGEST_CPP}
    COMMAND ${CMAKE_COMMAND} -DROOT=${SS_SRC} -DOUT=${SS_ROMA_DIGEST_CPP}
            -P ${SS_ROOT}/cmake/RomaModelDigest.cmake
    DEPENDS ${SS_ROMA_DIGEST_INPUTS} ${SS_ROOT}/cmake/RomaModelDigest.cmake
    COMMENT "roma model source digest" VERBATIM)
# CameraMath, SourceCamera and DepthPng are the engine's or the app's too; as archive members
# they are pulled in only where nothing else provides them, as ss_sfm does stb.
add_library(ss_roma_host STATIC ${SS_ROMA_HOST_SOURCES} ${SS_ROMA_DIGEST_CPP}
    ${SS_SRC}/data/CameraMath.cpp ${SS_SRC}/data/SourceCamera.cpp ${SS_SRC}/app/DepthPng.cpp)
target_include_directories(ss_roma_host PUBLIC ${SS_SRC})
target_link_libraries(ss_roma_host PUBLIC ss_sfm)
target_compile_options(ss_roma_host PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${SPLAT_CXX_FLAGS}>)
set_property(TARGET ss_roma_host PROPERTY CXX_STANDARD 17)

file(GLOB SS_ROMA_TESTS CONFIGURE_DEPENDS ${SS_SRC}/roma/tests/*.cpp)
foreach(test_src ${SS_ROMA_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE ss_roma_host)
    set_property(TARGET ${test_name} PROPERTY CXX_STANDARD 17)
    target_compile_options(${test_name} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${SPLAT_CXX_FLAGS}>)
endforeach()

if(SS_BUILD_SAM)
    set(SS_EXT_TOOL_SOURCES ${SS_EXT_TOOL_SOURCES} ${SS_SRC}/app/cli/densify_main.cpp)
    set(SS_EXT_TOOL_DEFS ${SS_EXT_TOOL_DEFS} SS_TOOL_DENSIFY=1)
    set(SS_EXT_TOOL_LIBS ${SS_EXT_TOOL_LIBS} ss_roma_host ss_roma)

    # A hand-run evaluation tool, not a test: it keeps the default test glob clean.
    add_executable(roma_match_pairs ${CMAKE_CURRENT_SOURCE_DIR}/tools/roma/roma_match_pairs.cpp)
    target_link_libraries(roma_match_pairs PRIVATE ss_roma_host ss_roma)
    set_property(TARGET roma_match_pairs PROPERTY CXX_STANDARD 17)
    target_compile_options(roma_match_pairs PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${SPLAT_CXX_FLAGS}>)
endif()

# The model test records the model's source digest in its dump manifest.
if(TARGET roma_model_test)
    target_link_libraries(roma_model_test PRIVATE ss_roma_host)
    target_compile_definitions(roma_model_test PRIVATE SS_ROMA_HOST=1)
endif()
