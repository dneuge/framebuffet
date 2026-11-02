CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
SET(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
SET(CMAKE_CXX_STANDARD 11)
PROJECT(fbsource_capture_macos_sck LANGUAGES C OBJC)

INCLUDE(CheckIncludeFile)

SET(CMAKE_BUILD_TYPE Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

MESSAGE(STATUS "Detected C compiler: ${CMAKE_C_COMPILER_ID}")
MESSAGE(STATUS "Detected C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
MESSAGE(STATUS "Detected Objective C compiler: ${CMAKE_OBJC_COMPILER_ID}")

ADD_DEFINITIONS(-DTARGET_MACOS)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/_build")
SET(SUPPORT_THREADS "${CMAKE_CURRENT_SOURCE_DIR}/../../support/threads")

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib)

SET(FBPLUGIN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/capture_macos_sck.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sck_capture.m
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sck_content.m
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils_objc.m
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.c
)

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

# check if C11 compatibility wrappers are needed for threads.h
CHECK_INCLUDE_FILE(threads.h HAVE_C11_THREADS)
IF(NOT HAVE_C11_THREADS)
    ADD_DEFINITIONS(-DNEED_C11_THREADS_WRAPPER)
ENDIF()

SET(FBPLUGIN_LINK_OPTIONS "")
#SET(FBPLUGIN_LINK_OPTIONS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export_symbols.map)

SET(LINK_FRAMEBUFFET_DYNAMIC "")

ADD_LIBRARY(fbsource_capture_macos_sck SHARED ${FBPLUGIN_SOURCES})

# we must not link to the framework or the module will try to implicitly load it under a hard-coded path,
# MacOS needs to be instructed to ignore unknown symbols if they are supposed to be provided by the runtime environment
TARGET_LINK_OPTIONS(fbsource_capture_macos_sck PRIVATE -undefined dynamic_lookup)

#ADD_LIBRARY(framebuffet SHARED IMPORTED)
#SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/libframebuffet.dylib")
#SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)

ADD_LIBRARY(fbsource_capture_macos_sck_static STATIC ${FBPLUGIN_SOURCES})
TARGET_COMPILE_DEFINITIONS(fbsource_capture_macos_sck_static PRIVATE FBSOURCE_BUILD_STATIC_MODULE)

TARGET_INCLUDE_DIRECTORIES(fbsource_capture_macos_sck PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})
TARGET_INCLUDE_DIRECTORIES(fbsource_capture_macos_sck_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})

TARGET_LINK_LIBRARIES(fbsource_capture_macos_sck ${LINK_FRAMEBUFFET_DYNAMIC} ${LINK_C11_COMPAT_STATIC})
TARGET_LINK_LIBRARIES(fbsource_capture_macos_sck_static ${LINK_C11_COMPAT_STATIC})

SET_PROPERTY(TARGET fbsource_capture_macos_sck APPEND_STRING PROPERTY LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -framework ScreenCaptureKit")

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_capture_macos_sck PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_capture_macos_sck PROPERTIES OUTPUT_NAME "fbsource_capture_macos_sck")

SET_TARGET_PROPERTIES(fbsource_capture_macos_sck_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_capture_macos_sck_static PROPERTIES OUTPUT_NAME "fbsource_capture_macos_sck")
