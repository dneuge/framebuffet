CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
SET(CMAKE_CXX_STANDARD 20)

PROJECT(fbsource_capture_winrt_dxgi)

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

IF(WIN32)
    ADD_DEFINITIONS(-DTARGET_WINDOWS)
ELSE()
    MESSAGE(WARNING "WinRT/DXGI capture source is only relevant for Windows and will not compile for any other target.")
ENDIF()

MESSAGE(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")

SET(LIB_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lib")
SET(LIB_BUILD "${LIB_BASE_DIR}/_build")
SET(SUPPORT_THREADS "${CMAKE_CURRENT_SOURCE_DIR}/../../support/threads")

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib)

SET(WIL_VERSION 1.0.260126.7)
SET(WIL_INCLUDE "${LIB_BASE_DIR}/wil/Microsoft.Windows.ImplementationLibrary.${WIL_VERSION}/include")

SET(WINDOWS_LIBS dwmapi D3D11 RuntimeObject)

SET(FBPLUGIN_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/capture_winrt_dxgi.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/winrt_dxgi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/window_utils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.c
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

ADD_LIBRARY(fbsource_capture_winrt_dxgi MODULE ${FBPLUGIN_SOURCES})
IF(MSVC)
    ADD_LIBRARY(framebuffet SHARED IMPORTED)
    #SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)
    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/framebuffet_dll_shim.lib")

    # TODO: "static lib" may be the wrong artifact due to extra symbols and name extension; do we need multiple artifacts and rename them?
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/framebuffet_static_lib.dll")
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_IMPLIB "${FRAMEBUFFET_LIB}/framebuffet_static_lib.lib")
ELSE()
    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/libframebuffet_dll_shim.a")

    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(fbsource_capture_winrt_dxgi PRIVATE -static-libgcc ${FBPLUGIN_LINK_OPTIONS})
ENDIF()

ADD_LIBRARY(fbsource_capture_winrt_dxgi_static STATIC ${FBPLUGIN_SOURCES})
TARGET_COMPILE_DEFINITIONS(fbsource_capture_winrt_dxgi_static PRIVATE FBSOURCE_BUILD_STATIC_MODULE)

TARGET_INCLUDE_DIRECTORIES(fbsource_capture_winrt_dxgi PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS} ${WIL_INCLUDE})
TARGET_INCLUDE_DIRECTORIES(fbsource_capture_winrt_dxgi_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS} ${WIL_INCLUDE})

TARGET_LINK_LIBRARIES(fbsource_capture_winrt_dxgi ${LINK_FRAMEBUFFET_DYNAMIC} ${LINK_C11_COMPAT_STATIC} ${WINDOWS_LIBS})
TARGET_LINK_LIBRARIES(fbsource_capture_winrt_dxgi_static ${WINDOWS_LIBS})

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_capture_winrt_dxgi PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_capture_winrt_dxgi PROPERTIES OUTPUT_NAME "fbsource_capture_winrt_dxgi")

SET_TARGET_PROPERTIES(fbsource_capture_winrt_dxgi_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_capture_winrt_dxgi_static PROPERTIES OUTPUT_NAME "fbsource_capture_winrt_dxgi")
IF(MSVC)
    SET_TARGET_PROPERTIES(fbsource_capture_winrt_dxgi_static PROPERTIES OUTPUT_NAME "fbsource_capture_winrt_dxgi_static")
ENDIF()
