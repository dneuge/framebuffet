CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
SET(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
SET(CMAKE_CXX_STANDARD 11)
PROJECT(fbsource_dummy)

INCLUDE(CheckIncludeFile)

SET(CMAKE_BUILD_TYPE Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

IF(CMAKE_VERSION VERSION_LESS "3.25")
    MESSAGE(WARNING "Your CMake version is outdated and does not detect Linux as such. Applying workaround to distinguish from MacOS. Update to CMake 3.25 or later if you can.")
    IF(UNIX)
        IF(NOT APPLE)
            MESSAGE(STATUS "Detected UNIX but not APPLE, assuming we are building for LINUX...")
            SET(LINUX TRUE)
        ENDIF()
    ENDIF()
ENDIF()

MESSAGE(STATUS "Detected C compiler: ${CMAKE_C_COMPILER_ID}")
MESSAGE(STATUS "Detected C++ compiler: ${CMAKE_CXX_COMPILER_ID}")

IF(CMAKE_GENERATOR MATCHES "Visual Studio")
    MESSAGE(STATUS "Detected Visual Studio")
    SET(MSVC TRUE)

    # VS should only ever be instructed to do release builds:
    #   1. debug builds may link against versions of standard libraries which
    #      cannot be obtained without Visual Studio and do not allow redistribution
    #   2. debug builds define _ITERATOR_DEBUG_LEVEL to values incompatible with
    #      libraries to be linked
    SET(CMAKE_BUILD_TYPE Release)
ENDIF()

IF(MSVC)
    # we end up with no symbols being exported by Visual Studio unless we specify them or enable this flag
    # see https://www.scivision.dev/cmake-shared-libs-visual-studio-windows/
    SET(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS On)
ENDIF()

IF(LINUX)
    ADD_DEFINITIONS(-DTARGET_LINUX)
ELSEIF(APPLE)
    ADD_DEFINITIONS(-DTARGET_MACOS)
ELSEIF(WIN32)
    ADD_DEFINITIONS(-DTARGET_WINDOWS)
ENDIF()

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/_build")
SET(SUPPORT_THREADS "${CMAKE_CURRENT_SOURCE_DIR}/../../support/threads")

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib)

SET(FBPLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dummy.c
)

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

# check if C11 compatibility wrappers are needed for threads.h and subsequently time.h
CHECK_INCLUDE_FILE(threads.h HAVE_C11_THREADS)
IF(NOT HAVE_C11_THREADS)
    ADD_DEFINITIONS(-DNEED_C11_THREADS_WRAPPER)
    IF(WIN32)
        SET(LINK_C11_COMPAT_STATIC "${LIB_BUILD}/libmesa_c11.a")
    ENDIF()
ENDIF()

SET(FBPLUGIN_LINK_OPTIONS "")
#SET(FBPLUGIN_LINK_OPTIONS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export_symbols.map)

SET(LINK_FRAMEBUFFET_DYNAMIC "")

IF(LINUX OR APPLE)
    ADD_LIBRARY(fbsource_dummy SHARED ${FBPLUGIN_SOURCES})
ELSEIF(WIN32)
    ADD_LIBRARY(fbsource_dummy MODULE ${FBPLUGIN_SOURCES})
ENDIF()

IF(APPLE)
    # we must not link to the framework or the module will try to implicitly load it under a hard-coded path,
    # MacOS needs to be instructed to ignore unknown symbols if they are supposed to be provided by the runtime environment
    TARGET_LINK_OPTIONS(fbsource_dummy PRIVATE -undefined dynamic_lookup)

    #ADD_LIBRARY(framebuffet SHARED IMPORTED)
    #SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/libframebuffet.dylib")
    #SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)
ELSEIF(MSVC)
    ADD_LIBRARY(framebuffet SHARED IMPORTED)
    #SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)
    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/framebuffet_dll_shim.lib")

    # TODO: "static lib" may be the wrong artifact due to extra symbols and name extension; do we need multiple artifacts and rename them?
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/framebuffet_static_lib.dll")
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_IMPLIB "${FRAMEBUFFET_LIB}/framebuffet_static_lib.lib")
ELSEIF(WIN32)
    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/libframebuffet_dll_shim.a")

    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(fbsource_dummy PRIVATE -static-libgcc ${FBPLUGIN_LINK_OPTIONS})
ENDIF()

ADD_LIBRARY(fbsource_dummy_static STATIC ${FBPLUGIN_SOURCES})
TARGET_COMPILE_DEFINITIONS(fbsource_dummy_static PRIVATE FBSOURCE_BUILD_STATIC_MODULE)

TARGET_INCLUDE_DIRECTORIES(fbsource_dummy PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})
TARGET_INCLUDE_DIRECTORIES(fbsource_dummy_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})

TARGET_LINK_LIBRARIES(fbsource_dummy ${LINK_FRAMEBUFFET_DYNAMIC} ${LINK_C11_COMPAT_STATIC})

IF(NOT APPLE AND NOT MSVC)
    ADD_CUSTOM_TARGET(fbsource_dummy_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:fbsource_dummy_static> ${LINK_C11_COMPAT_STATIC})
ENDIF()

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_dummy PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_dummy PROPERTIES OUTPUT_NAME "fbsource_dummy")

SET_TARGET_PROPERTIES(fbsource_dummy_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_dummy_static PROPERTIES OUTPUT_NAME "fbsource_dummy")
IF(MSVC)
    SET_TARGET_PROPERTIES(fbsource_dummy_static PROPERTIES OUTPUT_NAME "fbsource_dummy_static")
ENDIF()
