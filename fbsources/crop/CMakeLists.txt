CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
SET(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
SET(CMAKE_CXX_STANDARD 11)
PROJECT(fbsource_crop)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crop.c
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
    #ADD_LIBRARY(framebuffet INTERFACE)
    
    ADD_LIBRARY(fbsource_crop SHARED ${FBPLUGIN_SOURCES})
ELSEIF(WIN32)
    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/libframebuffet_dll_shim.a")

    ADD_LIBRARY(fbsource_crop MODULE ${FBPLUGIN_SOURCES})

    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(fbsource_crop PRIVATE -static-libgcc ${FBPLUGIN_LINK_OPTIONS})
ENDIF()

IF(APPLE)
    # we must not link to the framework or the module will try to implicitly load it under a hard-coded path,
    # MacOS needs to be instructed to ignore unknown symbols if they are supposed to be provided by the runtime environment
    TARGET_LINK_OPTIONS(fbsource_crop PRIVATE -undefined dynamic_lookup)

    #ADD_LIBRARY(framebuffet SHARED IMPORTED)
    #SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/libframebuffet.dylib")
    #SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)
ENDIF()

ADD_LIBRARY(fbsource_crop_static STATIC ${FBPLUGIN_SOURCES})
TARGET_COMPILE_DEFINITIONS(fbsource_crop_static PRIVATE FBSOURCE_BUILD_STATIC_MODULE)

TARGET_INCLUDE_DIRECTORIES(fbsource_crop PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})
TARGET_INCLUDE_DIRECTORIES(fbsource_crop_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})

TARGET_LINK_LIBRARIES(fbsource_crop ${LINK_FRAMEBUFFET_DYNAMIC} ${LINK_C11_COMPAT_STATIC})
TARGET_LINK_LIBRARIES(fbsource_crop_static ${LINK_C11_COMPAT_STATIC})

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_crop PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_crop PROPERTIES OUTPUT_NAME "fbsource_crop")

SET_TARGET_PROPERTIES(fbsource_crop_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_crop_static PROPERTIES OUTPUT_NAME "fbsource_crop")
