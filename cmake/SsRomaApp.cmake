# The RoMa tests that need the app's settings, so they come after SsApps.cmake
# (ss_configure_app). The densify tool itself is wired in SsRoma.cmake.

if(SS_BUILD_SFM AND SS_BUILD_SAM)
    # Gate H-3: a densified sibling model never wins the parser's automatic pick.
    add_executable(densify_autopick_test ${SS_SRC}/app/tests/densify_autopick_test.cpp)
    ss_configure_app(densify_autopick_test)
    target_link_libraries(densify_autopick_test PRIVATE ss_roma_host)
endif()

if(SS_BUILD_GUI)
    # The argument list `spirula densify` is handed, and the model chooser's listing.
    add_executable(densify_gui_test
        ${SS_SRC}/app/gui/tests/densify_gui_test.cpp
        ${SS_SRC}/app/gui/DensifyArgs.cpp
        ${SS_SRC}/app/gui/ReconModels.cpp)
    ss_configure_app(densify_gui_test)
    if(TARGET ss_roma)
        # The GUI's preset names and help are held to the matcher's own.
        target_link_libraries(densify_gui_test PRIVATE ss_roma)
        target_compile_definitions(densify_gui_test PRIVATE SS_ROMA_LINKED=1)
    endif()
endif()
