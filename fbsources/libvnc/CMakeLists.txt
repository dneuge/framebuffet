CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
PROJECT(fbsource_libvnc)

INCLUDE(CheckIncludeFile)

SET(CMAKE_BUILD_TYPE Debug)
#SET(CMAKE_VERBOSE_MAKEFILE On)

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib)

SET(FBPLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbsource_libvnc.c
)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/_build")

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

# check if C11 compatibility wrappers are needed for threads.h and subsequently time.h
CHECK_INCLUDE_FILE(threads.h HAVE_C11_THREADS)
IF(HAVE_C11_THREADS)
    ADD_DEFINITIONS(-DHAVE_C11_THREADS)
ELSE()
    SET(LINK_C11_COMPAT_STATIC "${LIB_BUILD}/libmesa_c11.a")
ENDIF()

# configure plugin
ADD_LIBRARY(libvncclient SHARED IMPORTED)

SET(ZLIB_NAME "libz")
SET(ZLIB_STATIC_NAME "${ZLIB_NAME}")

SET(FBPLUGIN_LINK_OPTIONS "")
#SET(FBPLUGIN_LINK_OPTIONS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export_symbols.map)

IF(UNIX)
    #ADD_LIBRARY(libvncclient OBJECT "${LIB_BUILD}/libvncclient.so")
    SET_TARGET_PROPERTIES(libvncclient PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libvncclient.so")
    
    ADD_LIBRARY(fbsource_libvnc SHARED ${FBPLUGIN_SOURCES})
    ADD_LIBRARY(fbsource_libvnc_static_module SHARED ${FBPLUGIN_SOURCES})
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

# MinGW is really pedantic about the order in which static dependencies are defined:
# users have to come before dependencies, otherwise imported symbols are discarded despite already having been presented
# Linux GCC on the other hand does not seem to care
SET(LIBVNCCLIENT_STATIC "${LIB_BUILD}/libvncclient.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/${ZLIB_STATIC_NAME}.a")

ADD_LIBRARY(fbsource_libvnc_static STATIC ${FBPLUGIN_SOURCES})

# FIXME: this seems highly wrong and doesn't display all expected symbols when viewed with mc, what's wrong?
# NOTE: CMAKE_C_ARCHIVE_CREATE does not work as it seems to not expand nested variables or something?
# https://stackoverflow.com/questions/14199708/cmake-include-library-dependencies-in-a-static-library
ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:fbsource_libvnc_static> ${LIBVNCCLIENT_STATIC})
#ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:fbsource_libvnc_static> "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/${ZLIB_STATIC_NAME}.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libvncclient.a")
#ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_C_ARCHIVE_CREATE} fbsource_libvnc_complete.${CMAKE_STATIC_LIBRARY_SUFFIX} $<TARGET_FILE:fbsource_libvnc_static> "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/libz.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libvncclient.a")

IF(WIN32)
    # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
    TARGET_LINK_OPTIONS(fbsource_libvnc_static_module PRIVATE -static-libgcc)
    TARGET_LINK_OPTIONS(fbsource_libvnc_static PRIVATE -static-libgcc)
    
    # libvncclient uses Winsock2 which is provided by MinGW's ws2_32
    # (must be added last as explained above or symbols will not be found!)
    SET(LIBVNCCLIENT_STATIC ${LIBVNCCLIENT_STATIC} ws2_32)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD})
TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc_static_module PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD})
TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD})

TARGET_LINK_LIBRARIES(fbsource_libvnc ${LINK_FRAMEBUFFET_DYNAMIC} libvncclient ${LINK_C11_COMPAT_STATIC})
TARGET_LINK_LIBRARIES(fbsource_libvnc_static_module ${LINK_FRAMEBUFFET_DYNAMIC} ${LIBVNCCLIENT_STATIC} ${LINK_C11_COMPAT_STATIC})
TARGET_LINK_LIBRARIES(fbsource_libvnc_static ${LIBVNCCLIENT_STATIC} ${LINK_C11_COMPAT_STATIC})

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES OUTPUT_NAME "fbsource_libvnc")

SET_TARGET_PROPERTIES(fbsource_libvnc_static_module PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc_static_module PROPERTIES OUTPUT_NAME "fbsource_libvnc_static")

SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES OUTPUT_NAME "fbsource_libvnc")
