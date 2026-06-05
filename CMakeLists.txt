cmake_minimum_required(VERSION 3.20)
project(nozzle_tester VERSION 0.1.0 LANGUAGES C CXX)

option(NOZZLE_TESTER_BUILD_GUI "Build nozzle-tester GUI" ON)
option(NOZZLE_TESTER_BUILD_TESTS "Build nozzle-tester tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE NOZZLE_TESTER_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT NOZZLE_TESTER_GIT_SHA)
    set(NOZZLE_TESTER_GIT_SHA unknown)
endif()

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/nozzle
    OUTPUT_VARIABLE NOZZLE_TESTER_NOZZLE_CORE_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT NOZZLE_TESTER_NOZZLE_CORE_SHA)
    set(NOZZLE_TESTER_NOZZLE_CORE_SHA unknown)
endif()

add_subdirectory(deps/nozzle)

add_library(nozzle_tester_core STATIC
    src/core/evidence.cpp
    src/core/pattern.cpp
)
target_include_directories(nozzle_tester_core PUBLIC src)
target_compile_definitions(nozzle_tester_core PRIVATE
    NOZZLE_TESTER_REPO_SHA="${NOZZLE_TESTER_GIT_SHA}"
    NOZZLE_TESTER_NOZZLE_CORE_SHA="${NOZZLE_TESTER_NOZZLE_CORE_SHA}"
)
target_compile_options(nozzle_tester_core PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(nozzle-tester-cli src/cli/main.cpp)
target_link_libraries(nozzle-tester-cli PRIVATE nozzle_tester_core nozzle)
target_compile_options(nozzle-tester-cli PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(NOZZLE_TESTER_BUILD_GUI)
    add_subdirectory(libs/glfw)

    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui)
    add_library(nozzle_tester_imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(nozzle_tester_imgui PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    target_link_libraries(nozzle_tester_imgui PUBLIC glfw)

    if(APPLE)
        target_compile_definitions(nozzle_tester_imgui PUBLIC GL_SILENCE_DEPRECATION)
        target_link_libraries(nozzle_tester_imgui PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit")
    elseif(WIN32)
        target_link_libraries(nozzle_tester_imgui PUBLIC opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(nozzle_tester_imgui PUBLIC OpenGL::GL)
    endif()

    if(APPLE)
        add_executable(nozzle-tester MACOSX_BUNDLE src/gui/main_gui.cpp)
        set_target_properties(nozzle-tester PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "nozzle-tester"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.nozzle-io.tester"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT "Copyright nozzle contributors"
        )
    else()
        add_executable(nozzle-tester src/gui/main_gui.cpp)
    endif()
    target_link_libraries(nozzle-tester PRIVATE nozzle_tester_core nozzle_tester_imgui nozzle)
    target_compile_options(nozzle-tester PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()

if(NOZZLE_TESTER_BUILD_TESTS)
    include(CTest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(nozzle_tester_tests tests/test_main.cpp)
    target_link_libraries(nozzle_tester_tests PRIVATE nozzle_tester_core)
    add_test(NAME nozzle_tester_unit COMMAND nozzle_tester_tests)
    add_test(NAME nozzle_tester_cli_self_test COMMAND nozzle-tester-cli self-test --evidence nozzle-tester-self-test.json)
    add_test(
        NAME nozzle_tester_cli_discovery_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/discovery_smoke.py
            --cli $<TARGET_FILE:nozzle-tester-cli>
    )
    set_tests_properties(nozzle_tester_cli_discovery_smoke PROPERTIES SKIP_RETURN_CODE 77)
endif()
