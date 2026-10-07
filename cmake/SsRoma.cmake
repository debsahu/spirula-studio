# The densify host stage (src/roma/*.cpp): view and pair selection,
# certainty sampling, triangulation with known poses, filters and the
# sibling-model writer. Host code on top of ss_sfm; the RoMa v2 network itself
# (src/roma/model/) is a separate inference-layer library. docs/notes/densify.md.
#
# Defines:
#   ss_roma_host     the static library
#   roma_*_test      one executable per src/roma/tests/*.cpp

file(GLOB SS_ROMA_HOST_SOURCES CONFIGURE_DEPENDS ${SS_SRC}/roma/*.cpp)
# CameraMath.cpp and SourceCamera.cpp are the engine's too; as archive members
# they are pulled in only where nothing else provides them, as ss_sfm does stb.
add_library(ss_roma_host STATIC ${SS_ROMA_HOST_SOURCES}
    ${SS_SRC}/data/CameraMath.cpp ${SS_SRC}/data/SourceCamera.cpp)
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
