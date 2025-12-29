# include from build bash scripts
# first parameter passed to any script including this file is expected to be the build target name
# 
# root_dir must be set to the root directory of framebuffet before including this file

BUILD_TARGET="linux"
CMAKE_TOOLCHAIN_FILE=""
if [[ "$#" -ge 1 ]]; then
	BUILD_TARGET="$1"
fi
export BUILD_TARGET

HOST_OS_NAME="$([[ -f /etc/os-release ]] && source /etc/os-release && echo $NAME)"
HOST_OS_VERSION="$([[ -f /etc/os-release ]] && source /etc/os-release && [[ "$NAME" == "Ubuntu" ]] && echo $UBUNTU_CODENAME)"
if [[ "${HOST_OS_NAME}" == "" ]]; then
	HOST_OS_NAME="unknown host system"
fi
export HOST_OS_NAME
export HOST_OS_VERSION

BUILD_TARGET_DYNLIB_EXT="so"
if [[ "${BUILD_TARGET}" == "windows" ]]; then
	BUILD_TARGET_DYNLIB_EXT="dll"
elif [[ "${BUILD_TARGET}" != "linux" ]]; then
	die "Unknown build target: ${BUILD_TARGET}"
fi
export BUILD_TARGET_DYNLIB_EXT

CMAKE_TOOLCHAIN_FILE=""
if [[ "${CMAKE_TOOLCHAIN_FILE:-}" != "" ]]; then
	echo "Using user-provided CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}"
elif [[ "${BUILD_TARGET}" == "linux" ]]; then
	CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-generic_linux-linux-x86_64-clang.cmake"
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
	if [[ "${HOST_OS_NAME} ${HOST_OS_VERSION}" == "Ubuntu jammy" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-ubuntu22.04-windows-x86_64-mingw.cmake"
	elif [[ "${HOST_OS_NAME}" == "Windows" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-windows_github-windows-x86_64-msvc_clang.cmake"
	else
		die "Missing CMake toolchain for ${HOST_OS_NAME} ${HOST_OS_VERSION} (target: ${BUILD_TARGET})"
	fi
fi

override_distribution_restrictions=0
if [[ "${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" == "1" || "${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" == "True" ]]; then
  override_distribution_restrictions=1
fi

CC="clang"
CPP_COMPILER="clang++"
if [[ "${BUILD_TARGET}" == "windows" ]]; then
  if [[ $override_distribution_restrictions -eq 1 ]]; then
    echo "!!! ENABLING MINGW COMPILATION WHICH VOIDS LICENSE CONFORMITY; DO NOT DISTRIBUTE BUILD RESULTS !!!"
    CC="x86_64-w64-mingw32-gcc"
    CPP_COMPILER="x86_64-w64-mingw32-g++"
  fi
fi
export CC
export CPP_COMPILER

# normalize paths
if [[ "${CMAKE_TOOLCHAIN_FILE}" != "" ]]; then
	CMAKE_TOOLCHAIN_FILE=$(realpath "${CMAKE_TOOLCHAIN_FILE}")
fi

echo "Build target: ${BUILD_TARGET}"
if [[ "${CMAKE_TOOLCHAIN_FILE}" != "" ]]; then
	echo "  applying CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}"
	export CMAKE_TOOLCHAIN_FILE
else
	echo "  no CMake toolchain"
fi
echo
