CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
SET(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
SET(CMAKE_CXX_STANDARD 11)
PROJECT(fbsource_libvnc)

INCLUDE(CheckIncludeFile)

SET(CMAKE_BUILD_TYPE Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
#SET(CMAKE_VERBOSE_MAKEFILE On)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbsource_libvnc.c
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

# configure plugin
ADD_LIBRARY(libvncclient SHARED IMPORTED)

SET(ZLIB_NAME "libz")
SET(ZLIB_STATIC_NAME "${ZLIB_NAME}")

SET(FBPLUGIN_LINK_OPTIONS "")
#SET(FBPLUGIN_LINK_OPTIONS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export_symbols.map)

IF(LINUX)
    #ADD_LIBRARY(libvncclient OBJECT "${LIB_BUILD}/libvncclient.so")
    SET_TARGET_PROPERTIES(libvncclient PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libvncclient.so")
    
    ADD_LIBRARY(fbsource_libvnc SHARED ${FBPLUGIN_SOURCES})
    ADD_LIBRARY(fbsource_libvnc_static_module SHARED ${FBPLUGIN_SOURCES})
ELSEIF(APPLE)
    #ADD_LIBRARY(framebuffet SHARED IMPORTED)
    #SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/libframebuffet.dylib")
    #SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)

    SET_TARGET_PROPERTIES(libvncclient PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libvncclient.dylib")

    #ADD_LIBRARY(fbsource_libvnc SHARED ${FBPLUGIN_SOURCES})
    ADD_LIBRARY(fbsource_libvnc_static_module SHARED ${FBPLUGIN_SOURCES})

    # we must not link to the framework or the module will try to implicitly load it under a hard-coded path,
    # MacOS needs to be instructed to ignore unknown symbols if they are supposed to be provided by the runtime environment
    TARGET_LINK_OPTIONS(fbsource_libvnc_static_module PRIVATE -undefined dynamic_lookup)
ELSEIF(MSVC)
    ADD_LIBRARY(framebuffet SHARED IMPORTED)
    #SET(LINK_FRAMEBUFFET_DYNAMIC framebuffet)
    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/framebuffet_dll_shim.lib")

    # TODO: "static lib" may be the wrong artifact due to extra symbols and name extension; do we need multiple artifacts and rename them?
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_LOCATION "${FRAMEBUFFET_LIB}/framebuffet_static_lib.dll")
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES IMPORTED_IMPLIB "${FRAMEBUFFET_LIB}/framebuffet_static_lib.lib")

    SET(ZLIB_STATIC_NAME "zlibstat")

    # VNC cannot be linked dynamically with Visual Studio as it only builds static binaries
    ADD_LIBRARY(fbsource_libvnc_static_module MODULE ${FBPLUGIN_SOURCES})
ELSEIF(WIN32)
    SET(ZLIB_NAME "libzlib")
    SET(ZLIB_STATIC_NAME "${ZLIB_NAME}static")

    SET_TARGET_PROPERTIES(libvncclient PROPERTIES IMPORTED_IMPLIB "${LIB_BUILD}/libvncclient.dll")

    SET(LINK_FRAMEBUFFET_DYNAMIC "${FRAMEBUFFET_LIB}/libframebuffet_dll_shim.a")

    ADD_LIBRARY(fbsource_libvnc MODULE ${FBPLUGIN_SOURCES})
    ADD_LIBRARY(fbsource_libvnc_static_module MODULE ${FBPLUGIN_SOURCES})

    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(fbsource_libvnc PRIVATE -static-libgcc ${FBPLUGIN_LINK_OPTIONS})
    TARGET_LINK_OPTIONS(fbsource_libvnc_static_module PRIVATE -static-libgcc ${FBPLUGIN_LINK_OPTIONS})
ENDIF()

IF(APPLE)
    SET(LIBVNCCLIENT_STATIC "${LIB_BUILD}/libvncclient.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/${ZLIB_STATIC_NAME}.a")
ELSEIF(MSVC)
    SET(LIBVNCCLIENT_STATIC "${LIB_BUILD}/vncclient.lib" "${LIB_BUILD}/turbojpeg-static.lib" "${LIB_BUILD}/jpeg-static.lib" "${LIB_BUILD}/lzo_object_lib.lib" "${LIB_BUILD}/${ZLIB_STATIC_NAME}.lib")
ELSE()
    # MinGW is really pedantic about the order in which static dependencies are defined:
    # users have to come before dependencies, otherwise imported symbols are discarded despite already having been presented
    # Linux GCC on the other hand does not seem to care
    SET(LIBVNCCLIENT_STATIC "${LIB_BUILD}/libvncclient.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/${ZLIB_STATIC_NAME}.a")
ENDIF()

ADD_LIBRARY(fbsource_libvnc_static STATIC ${FBPLUGIN_SOURCES})
TARGET_COMPILE_DEFINITIONS(fbsource_libvnc_static PRIVATE FBSOURCE_BUILD_STATIC_MODULE)

IF(NOT APPLE AND NOT MSVC)
    # FIXME: this seems highly wrong and doesn't display all expected symbols when viewed with mc, what's wrong?
    # NOTE: CMAKE_C_ARCHIVE_CREATE does not work as it seems to not expand nested variables or something?
    # https://stackoverflow.com/questions/14199708/cmake-include-library-dependencies-in-a-static-library
    ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:fbsource_libvnc_static> ${LIBVNCCLIENT_STATIC})
    #ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:fbsource_libvnc_static> "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/${ZLIB_STATIC_NAME}.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libvncclient.a")
    #ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_C_ARCHIVE_CREATE} fbsource_libvnc_complete.${CMAKE_STATIC_LIBRARY_SUFFIX} $<TARGET_FILE:fbsource_libvnc_static> "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/libz.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libvncclient.a")
ENDIF()

IF(WIN32 AND NOT MSVC)
    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(fbsource_libvnc_static_module PRIVATE -static-libgcc)
    TARGET_LINK_OPTIONS(fbsource_libvnc_static PRIVATE -static-libgcc)
    
    # libvncclient uses Winsock2 which is provided by MinGW's ws2_32
    # (must be added last as explained above or symbols will not be found!)
    SET(LIBVNCCLIENT_STATIC ${LIBVNCCLIENT_STATIC} ws2_32)
ELSEIF(MSVC)
    # Visual Studio needs the same but under a slightly different name
    SET(LIBVNCCLIENT_STATIC ${LIBVNCCLIENT_STATIC} WS2_32.lib)
ENDIF()

IF(NOT APPLE AND NOT MSVC)
    TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})
    TARGET_LINK_LIBRARIES(fbsource_libvnc ${LINK_FRAMEBUFFET_DYNAMIC} libvncclient ${LINK_C11_COMPAT_STATIC})
ENDIF()

TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc_static_module PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})
TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD} ${SUPPORT_THREADS})

TARGET_LINK_LIBRARIES(fbsource_libvnc_static_module ${LINK_FRAMEBUFFET_DYNAMIC} ${LIBVNCCLIENT_STATIC} ${LINK_C11_COMPAT_STATIC})
TARGET_LINK_LIBRARIES(fbsource_libvnc_static ${LIBVNCCLIENT_STATIC} ${LINK_C11_COMPAT_STATIC})

# compiled output file name
IF(NOT APPLE AND NOT MSVC)
    SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES PREFIX "")
    SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES OUTPUT_NAME "fbsource_libvnc")
ENDIF()

SET_TARGET_PROPERTIES(fbsource_libvnc_static_module PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc_static_module PROPERTIES OUTPUT_NAME "fbsource_libvnc_static")

SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES OUTPUT_NAME "fbsource_libvnc")
