CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)

PROJECT(fbsourceviewer)

SET(CMAKE_BUILD_TYPE Debug)

SET(FRAMEBUFFET_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/include)
SET(FRAMEBUFFET_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../framework/release/lib/libframebuffet.so)

SET(VIEWER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbsourceviewer.c
)

FIND_PACKAGE(SDL2 REQUIRED)

# currently we only support local compilation on Linux,
# so we should not have any need for C11 compatibility wrappers
ADD_DEFINITIONS(-DHAVE_C11_THREADS)

INCLUDE_DIRECTORIES(fbsourceviewer "${SDL2_INCLUDE_DIRS}" ${FRAMEBUFFET_INCLUDE})

ADD_EXECUTABLE(fbsourceviewer ${VIEWER_SOURCES})
TARGET_LINK_LIBRARIES(fbsourceviewer SDL2::SDL2 SDL2::SDL2main ${FRAMEBUFFET_LIB})
#ADD_LIBRARY(boost OBJECT "${BOOST_LIBRARYDIR}/libboost_atomic.a" "${BOOST_LIBRARYDIR}/libboost_filesystem.a")
