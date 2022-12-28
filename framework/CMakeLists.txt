CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
PROJECT(framebuffet)

INCLUDE(CheckIncludeFile)

SET(CMAKE_BUILD_TYPE Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

SET(FRAMEBUFFET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbcollections.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/framebuffet.c
)

IF(WIN32)
    SET(FRAMEBUFFET_DLL_SHIM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/framebuffet_dll_shim.c
    )
ENDIF()

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../lib/_build")

# check if C11 compatibility wrappers are needed for threads.h and subsequently time.h
CHECK_INCLUDE_FILE(threads.h HAVE_C11_THREADS)
IF(NOT HAVE_C11_THREADS)
    ADD_DEFINITIONS(-DNEED_C11_THREADS_WRAPPER)
    SET(LINK_C11_COMPAT_STATIC "${LIB_BUILD}/libmesa_c11.a")
ENDIF()

IF(UNIX)
    ADD_DEFINITIONS(-DTARGET_UNIX)
    
    ADD_LIBRARY(cjson SHARED IMPORTED)
    SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libcjson.so")
    
ELSEIF(WIN32)
    ADD_DEFINITIONS(-DTARGET_WINDOWS)
    
    ADD_LIBRARY(cjson SHARED IMPORTED)
    SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_IMPLIB "${LIB_BUILD}/libcjson.dll")

    #ADD_LIBRARY(framebuffet MODULE ${FRAMEBUFFET_SOURCES})
ENDIF()

# FIXME: framebuffet_static_lib does not export anything but cJSON symbols when compiled with MinGW

ADD_LIBRARY(framebuffet SHARED ${FRAMEBUFFET_SOURCES})
ADD_LIBRARY(framebuffet_static_lib SHARED ${FRAMEBUFFET_SOURCES})

ADD_LIBRARY(framebuffet_static STATIC ${FRAMEBUFFET_SOURCES})

TARGET_INCLUDE_DIRECTORIES(framebuffet PUBLIC ${LIB_BUILD})
TARGET_INCLUDE_DIRECTORIES(framebuffet_static_lib PUBLIC ${LIB_BUILD})
TARGET_INCLUDE_DIRECTORIES(framebuffet_static PUBLIC ${LIB_BUILD})

SET(FRAMEBUFFET_LINK_OPTIONS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export_symbols.map)

IF(WIN32)
    # by default all DLLs get prefixed with "lib" when built with CMake and MinGW
    # which is very unusual and should be avoided (incompatible with Java native library lookup etc.)
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES PREFIX "")
    SET_TARGET_PROPERTIES(framebuffet_static_lib PROPERTIES PREFIX "")

    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(framebuffet PRIVATE -static-libgcc ${FRAMEBUFFET_LINK_OPTIONS})
    TARGET_LINK_OPTIONS(framebuffet_static_lib PRIVATE -static-libgcc ${FRAMEBUFFET_LINK_OPTIONS})

    ADD_LIBRARY(framebuffet_dll_shim STATIC ${FRAMEBUFFET_DLL_SHIM_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(framebuffet_dll_shim PUBLIC ${LIB_BUILD})
ENDIF()

SET_TARGET_PROPERTIES(framebuffet_static PROPERTIES OUTPUT_NAME "framebuffet")

# FIXME: this seems highly wrong and doesn't display all expected symbols when viewed with mc, what's wrong?
# NOTE: CMAKE_C_ARCHIVE_CREATE does not work as it seems to not expand nested variables or something?
# https://stackoverflow.com/questions/14199708/cmake-include-library-dependencies-in-a-static-library
ADD_CUSTOM_TARGET(framebuffet_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:framebuffet_static> "${LIB_BUILD}/libcjson.a" "${LINK_C11_COMPAT_STATIC}")

TARGET_LINK_LIBRARIES(framebuffet cjson ${LINK_C11_COMPAT_STATIC})
TARGET_LINK_LIBRARIES(framebuffet_static_lib "${LIB_BUILD}/libcjson.a" ${LINK_C11_COMPAT_STATIC})
