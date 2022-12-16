CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
PROJECT(fbsource_libvnc)

SET(CMAKE_BUILD_TYPE Debug)

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib)

SET(FBPLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbsource_libvnc.c
)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/_build")

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

# configure plugin
IF(UNIX)
    #ADD_LIBRARY(libvncclient OBJECT "${LIB_BUILD}/libvncclient.so")
    ADD_LIBRARY(libvncclient SHARED IMPORTED)
    SET_TARGET_PROPERTIES(libvncclient PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libvncclient.so")
    
    ADD_LIBRARY(fbsource_libvnc SHARED ${FBPLUGIN_SOURCES})
    ADD_LIBRARY(fbsource_libvnc_static STATIC ${FBPLUGIN_SOURCES})
    
    # FIXME: this seems highly wrong and doesn't display all expected symbols when viewed with mc, what's wrong?
    # NOTE: CMAKE_C_ARCHIVE_CREATE does not work as it seems to not expand nested variables or something?
    # https://stackoverflow.com/questions/14199708/cmake-include-library-dependencies-in-a-static-library
    ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:fbsource_libvnc_static> "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/libz.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libvncclient.a")
    #ADD_CUSTOM_TARGET(fbsource_libvnc_static_complete ALL COMMAND ${CMAKE_C_ARCHIVE_CREATE} fbsource_libvnc_complete.${CMAKE_STATIC_LIBRARY_SUFFIX} $<TARGET_FILE:fbsource_libvnc_static> "${LIB_BUILD}/liblzo2.a" "${LIB_BUILD}/libz.a" "${LIB_BUILD}/libjpeg.a" "${LIB_BUILD}/libturbojpeg.a" "${LIB_BUILD}/libvncclient.a")
ELSEIF(WIN32)
    ADD_LIBRARY(fbsource_libvnc MODULE ${FBPLUGIN_SOURCES})
ENDIF()

TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD})
TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc_static PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD})

TARGET_LINK_LIBRARIES(fbsource_libvnc libvncclient)
TARGET_LINK_LIBRARIES(fbsource_libvnc_static libvncclient_static)

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES OUTPUT_NAME "fbsource_libvnc")

SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES OUTPUT_NAME "fbsource_libvnc")
