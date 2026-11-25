cmake_minimum_required(VERSION 3.16)

project(pluto_link
        VERSION 0.4.0
        DESCRIPTION "Full-duplex IQ TX/RX for two ADALM-Pluto boards, QPSK receiver with GUI"
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_GUI "Build pluto_gui (needs SDL2, OpenGL, thirdparty/imgui, thirdparty/implot)" ON)
option(BUILD_TESTS "Build dsp_tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

add_executable(pluto_link src/main.c)
target_compile_options(pluto_link PRIVATE -Wall -Wextra)


find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SOAPY_PC QUIET SoapySDR)
endif()

if(SOAPY_PC_FOUND)
    message(STATUS "SoapySDR ${SOAPY_PC_VERSION}: pkg-config (${SOAPY_PC_INCLUDEDIR})")
    target_include_directories(pluto_link PRIVATE ${SOAPY_PC_INCLUDE_DIRS})
    target_link_directories(pluto_link PRIVATE ${SOAPY_PC_LIBRARY_DIRS})
    target_link_libraries(pluto_link PRIVATE ${SOAPY_PC_LIBRARIES} m)
    target_compile_options(pluto_link PRIVATE ${SOAPY_PC_CFLAGS_OTHER})
else()
    find_package(SoapySDR CONFIG REQUIRED)
    message(STATUS "SoapySDR: find_package (${SoapySDR_INCLUDE_DIRS})")
    target_include_directories(pluto_link PRIVATE ${SoapySDR_INCLUDE_DIRS})
    target_link_directories(pluto_link PRIVATE ${SoapySDR_LIBRARY_DIRS})
    target_link_libraries(pluto_link PRIVATE ${SoapySDR_LIBRARIES} m)
endif()

find_package(Threads REQUIRED)

add_library(pluto_dsp STATIC src/backend.cpp)
target_include_directories(pluto_dsp PUBLIC src)
target_link_libraries(pluto_dsp PUBLIC Threads::Threads)
target_compile_options(pluto_dsp PRIVATE -Wall -Wextra)

if(BUILD_TESTS)
    enable_testing()
    add_executable(dsp_tests tests/test_dsp.cpp)
    target_link_libraries(dsp_tests PRIVATE pluto_dsp)
    target_compile_options(dsp_tests PRIVATE -Wall -Wextra)
    add_test(NAME dsp COMMAND dsp_tests)
endif()

if(BUILD_GUI)
    set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
    set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/thirdparty/implot)
    if(NOT EXISTS ${IMGUI_DIR}/imgui.h OR NOT EXISTS ${IMPLOT_DIR}/implot.h)
        message(FATAL_ERROR "thirdparty/imgui or thirdparty/implot is empty. Run: git submodule update --init --recursive (or configure with -DBUILD_GUI=OFF)")
    endif()
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    add_executable(pluto_gui
        src/gui.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMPLOT_DIR}/implot.cpp
        ${IMPLOT_DIR}/implot_items.cpp)
    target_include_directories(pluto_gui PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMPLOT_DIR})
    target_compile_options(pluto_gui PRIVATE -Wall)
    if(TARGET SDL2::SDL2)
        target_link_libraries(pluto_gui PRIVATE SDL2::SDL2)
    else()
        target_include_directories(pluto_gui PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(pluto_gui PRIVATE ${SDL2_LIBRARIES})
    endif()
    target_link_libraries(pluto_gui PRIVATE pluto_dsp OpenGL::GL)
    add_custom_command(TARGET pluto_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink pluto_link $<TARGET_FILE_DIR:pluto_gui>/main)
endif()

message(STATUS "Targets: pluto_link (RX/TX), pluto_gui (receiver GUI, ./build/pluto_gui --sim), dsp_tests (ctest)")
