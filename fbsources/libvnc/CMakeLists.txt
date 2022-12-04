CMAKE_MINIMUM_REQUIRED(VERSION 3.24)
PROJECT(fbsource_libvnc)

SET(CMAKE_BUILD_TYPE Debug)

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib)

SET(FBPLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbsource_libvnc.c
)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/_build")

# configure plugin
IF(UNIX)
    #ADD_LIBRARY(libvncclient OBJECT "${LIB_BUILD}/libvncclient.so")
    ADD_LIBRARY(libvncclient SHARED IMPORTED)
    SET_TARGET_PROPERTIES(libvncclient PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libvncclient.so")
    
    ADD_LIBRARY(fbsource_libvnc SHARED ${FBPLUGIN_SOURCES})
  
    #ADD_LIBRARY(fbsource_libvnc_static STATIC ${FBPLUGIN_SOURCES})
ELSEIF(WIN32)
    ADD_LIBRARY(fbsource_libvnc MODULE ${FBPLUGIN_SOURCES})
ENDIF()

TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc PUBLIC ${FRAMEBUFFET_INCLUDE} ${LIB_BUILD})
#TARGET_INCLUDE_DIRECTORIES(fbsource_libvnc_static PUBLIC ${FRAMEBUFFET_INCLUDE})

TARGET_LINK_LIBRARIES(fbsource_libvnc libvncclient)

# compiled output file name
SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(fbsource_libvnc PROPERTIES OUTPUT_NAME "fbsource_libvnc")

#SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES PREFIX "")
#SET_TARGET_PROPERTIES(fbsource_libvnc_static PROPERTIES OUTPUT_NAME "fbsource_libvnc")
