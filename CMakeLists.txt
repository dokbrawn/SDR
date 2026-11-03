cmake_minimum_required(VERSION 3.16)

project(pluto_link
        VERSION 0.3.0
        DESCRIPTION "Full-duplex IQ TX/RX for two ADALM-Pluto boards over SoapySDR"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

message(STATUS "Build finished. Run: ./build/pluto_link rx usb:3.4.5  (+ 'tx usb:3.5.5' in another terminal)")
