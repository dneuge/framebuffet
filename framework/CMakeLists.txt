CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)

PROJECT(framebuffet)

SET(CMAKE_BUILD_TYPE Debug)

SET(FRAMEBUFFET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbcollections.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/framebuffet.c
)

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../lib/_build")

IF(UNIX)
    ADD_LIBRARY(cjson SHARED IMPORTED)
    SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libcjson.so")
    
    ADD_LIBRARY(framebuffet SHARED ${FRAMEBUFFET_SOURCES})
    TARGET_COMPILE_DEFINITIONS(framebuffet PUBLIC -DTARGET_UNIX)
    
    ADD_LIBRARY(framebuffet_static STATIC ${FRAMEBUFFET_SOURCES})
    TARGET_COMPILE_DEFINITIONS(framebuffet_static PUBLIC -DTARGET_UNIX)
    SET_TARGET_PROPERTIES(framebuffet_static PROPERTIES OUTPUT_NAME "framebuffet")
    
    # FIXME: this seems highly wrong and doesn't display all expected symbols when viewed with mc, what's wrong?
    # NOTE: CMAKE_C_ARCHIVE_CREATE does not work as it seems to not expand nested variables or something?
    # https://stackoverflow.com/questions/14199708/cmake-include-library-dependencies-in-a-static-library
    ADD_CUSTOM_TARGET(framebuffet_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:framebuffet_static> "${LIB_BUILD}/libcjson.a")
ELSEIF(WIN32)
    ADD_LIBRARY(cjson SHARED IMPORTED)
    SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libcjson.dll")
    
    ADD_LIBRARY(framebuffet MODULE ${FRAMEBUFFET_SOURCES})
    TARGET_COMPILE_DEFINITIONS(framebuffet PUBLIC -DTARGET_WINDOWS)
ENDIF()

TARGET_LINK_LIBRARIES(framebuffet cjson)
