CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
SET(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
SET(CMAKE_CXX_STANDARD 11)
PROJECT(framebuffet)

# Output artifacts are:
#
#   framebuffet                  fully dynamically linked framework (dependencies need to be provided as shared objects at runtime)
#   framebuffet_static_lib       dynamically linked framework with statically linked dependencies (dependencies become part of the framework artifact)
#   framebuffet_static           just the framework to be statically linked later (linking dependencies is left to downstream projects)
#   framebuffet_static_complete  (EXPERIMENTAL) the framework and its dependencies all statically linked together into a single artifact
#
# The following dependencies are handled differently:
# - C11 threads wrappers are statically included even on the "fully dynamic" build
# - OS libraries are always dynamically linked

INCLUDE(CheckIncludeFile)
INCLUDE(CheckCSourceCompiles)

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

SET(FRAMEBUFFET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbcollections.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fbutil.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/framebuffet.c
)

IF(WIN32)
    SET(FRAMEBUFFET_DLL_SHIM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/framebuffet_dll_shim.c
    )
ENDIF()

ADD_DEFINITIONS(-DFBLOG_COMPILED_MIN_LOG_LEVEL=FBLOG_LEVEL_TRACE)

# unlock full header contents
ADD_DEFINITIONS(-DBUILD_FRAMEBUFFET_FRAMEWORK)

SET(LIB_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../lib/_build")
SET(SUPPORT_THREADS "${CMAKE_CURRENT_SOURCE_DIR}/../support/threads")

# check if C11 compatibility wrappers are needed for threads.h and subsequently time.h
CHECK_INCLUDE_FILE(threads.h HAVE_C11_THREADS)
IF(NOT HAVE_C11_THREADS)
    ADD_DEFINITIONS(-DNEED_C11_THREADS_WRAPPER)
    IF(WIN32)
        SET(LINK_C11_COMPAT_STATIC "${LIB_BUILD}/libmesa_c11.a")
    ENDIF()
ENDIF()

# check if pthreads can be linked against in the way we need it (optional)
SET(PTHREAD_LIB "")
IF(UNIX)
    SET(PTHREAD_TEST_SOURCE "\n
        #include <pthread.h>\n
        int main() {\n
            pthread_setname_np(pthread_self(), \"test\");\n
        }\n
    ")
    SET(PTHREAD_TEST_GNU_SOURCE "\n
        #define _GNU_SOURCE\n
        ${PTHREAD_TEST_SOURCE}
    ")
    CHECK_C_SOURCE_COMPILES("${PTHREAD_TEST_SOURCE}" HAVE_PTHREAD FAIL_REGEX "implicit declaration")
    CHECK_C_SOURCE_COMPILES("${PTHREAD_TEST_GNU_SOURCE}" HAVE_PTHREAD_NEED_GNU_SOURCE FAIL_REGEX "implicit declaration")
    IF(HAVE_PTHREAD OR HAVE_PTHREAD_NEED_GNU_SOURCE)
        SET(PTHREAD_LIB pthread)
        ADD_DEFINITIONS(-DHAVE_PTHREAD)
        MESSAGE(STATUS "set PTHREAD_LIB=${PTHREAD_LIB}, added definition HAVE_PTHREAD")
    ENDIF()
    
    IF(HAVE_PTHREAD_NEED_GNU_SOURCE)
        ADD_DEFINITIONS(-D_GNU_SOURCE)
        MESSAGE(STATUS "added definition _GNU_SOURCE")
    ENDIF()
ENDIF()

SET(LINK_CJSON_STATIC "${LIB_BUILD}/libcjson.a")
IF(LINUX)
    ADD_DEFINITIONS(-DTARGET_LINUX)
    
    ADD_LIBRARY(cjson SHARED IMPORTED)
    SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libcjson.so")
ELSEIF(APPLE)
    ADD_DEFINITIONS(-DTARGET_MACOS)
    
    ADD_LIBRARY(cjson SHARED IMPORTED)
    SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/libcjson.dylib")
ELSEIF(WIN32)
    ADD_DEFINITIONS(-DTARGET_WINDOWS)
    
    ADD_LIBRARY(cjson SHARED IMPORTED)

    IF(MSVC)
        SET(LINK_CJSON_STATIC "${LIB_BUILD}/libcjson.lib")

        SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_LOCATION "${LIB_BUILD}/cjson.dll")
        SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_IMPLIB "${LIB_BUILD}/cjson${CMAKE_STATIC_LIBRARY_SUFFIX}")
    ELSE()
        SET_TARGET_PROPERTIES(cjson PROPERTIES IMPORTED_IMPLIB "${LIB_BUILD}/libcjson.dll")
    ENDIF()

    #ADD_LIBRARY(framebuffet MODULE ${FRAMEBUFFET_SOURCES})
ENDIF()

# FIXME: framebuffet_static_lib does not export anything but cJSON symbols when compiled with MinGW

ADD_LIBRARY(framebuffet SHARED ${FRAMEBUFFET_SOURCES})
TARGET_INCLUDE_DIRECTORIES(framebuffet PUBLIC ${LIB_BUILD} ${SUPPORT_THREADS})

ADD_LIBRARY(framebuffet_static_lib SHARED ${FRAMEBUFFET_SOURCES})
TARGET_INCLUDE_DIRECTORIES(framebuffet_static_lib PUBLIC ${LIB_BUILD} ${SUPPORT_THREADS})

ADD_LIBRARY(framebuffet_static STATIC ${FRAMEBUFFET_SOURCES})
TARGET_INCLUDE_DIRECTORIES(framebuffet_static PUBLIC ${LIB_BUILD} ${SUPPORT_THREADS})

IF(WIN32)
    # by default all DLLs get prefixed with "lib" when built with CMake and MinGW
    # which is very unusual and should be avoided (incompatible with Java native library lookup etc.)
    SET_TARGET_PROPERTIES(framebuffet PROPERTIES PREFIX "")
    SET_TARGET_PROPERTIES(framebuffet_static_lib PROPERTIES PREFIX "")

    # TODO: define something like export_symbols for MSVC as well?
    IF(NOT MSVC)
        SET(FRAMEBUFFET_LINK_OPTIONS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export_symbols.map)

        # libgcc_s_seh will be missing on Windows if we don't compile with static-libgcc
        TARGET_LINK_OPTIONS(framebuffet PRIVATE -static-libgcc ${FRAMEBUFFET_LINK_OPTIONS})
        TARGET_LINK_OPTIONS(framebuffet_static_lib PRIVATE -static-libgcc ${FRAMEBUFFET_LINK_OPTIONS})
    ENDIF()

    ADD_LIBRARY(framebuffet_dll_shim STATIC ${FRAMEBUFFET_DLL_SHIM_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(framebuffet_dll_shim PUBLIC ${LIB_BUILD} ${SUPPORT_THREADS})
ENDIF()

SET_TARGET_PROPERTIES(framebuffet_static PROPERTIES OUTPUT_NAME "framebuffet")
IF(MSVC)
    SET_TARGET_PROPERTIES(framebuffet_static PROPERTIES OUTPUT_NAME "framebuffet_static")
ENDIF()

IF(NOT APPLE AND NOT MSVC)
    # FIXME: this seems highly wrong and doesn't display all expected symbols when viewed with mc, what's wrong?
    # NOTE: CMAKE_C_ARCHIVE_CREATE does not work as it seems to not expand nested variables or something?
    # https://stackoverflow.com/questions/14199708/cmake-include-library-dependencies-in-a-static-library
    ADD_CUSTOM_TARGET(framebuffet_static_complete ALL COMMAND ${CMAKE_AR} rc $<TARGET_FILE:framebuffet_static> "${LINK_CJSON_STATIC}" "${LINK_C11_COMPAT_STATIC}")
ENDIF()

TARGET_LINK_LIBRARIES(framebuffet cjson "${LINK_C11_COMPAT_STATIC}" ${PTHREAD_LIB})
TARGET_LINK_LIBRARIES(framebuffet_static_lib "${LINK_CJSON_STATIC}" "${LINK_C11_COMPAT_STATIC}" ${PTHREAD_LIB})
