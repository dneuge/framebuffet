# include from build bash scripts
# first parameter passed to any script including this file is expected to be the build target name
# 
# root_dir must be set to the root directory of framebuffet before including this file

function die() {
	echo $@ >&2
	exit 1
}

HOST_OS_TYPE="$(uname | cut -d' ' -f1)"
HOST_OS_ARCH="$(arch)"
HOST_OS_NAME=""
HOST_OS_VERSION=""
if [[ "${HOST_OS_TYPE}" == "Linux" ]]; then
	HOST_OS_NAME="$([[ -f /etc/os-release ]] && source /etc/os-release && echo $NAME)"
	HOST_OS_VERSION="$([[ -f /etc/os-release ]] && source /etc/os-release && [[ "$NAME" == "Ubuntu" ]] && echo $UBUNTU_CODENAME)"
elif [[ "${HOST_OS_TYPE}" == "Darwin" ]]; then
	HOST_OS_TYPE="MacOS"
	HOST_OS_NAME="MacOS"
	HOST_OS_VERSION="$(sw_vers -productVersion)"
fi
if [[ "${HOST_OS_NAME}" == "" ]]; then
	HOST_OS_NAME="unknown host system"
	echo "!!! Unknown host system: HOST_OS_NAME=${HOST_OS_NAME} / HOST_OS_TYPE=${HOST_OS_TYPE}"
fi
export HOST_OS_TYPE
export HOST_OS_ARCH
export HOST_OS_NAME
export HOST_OS_VERSION

BUILD_TARGET="$(tr '[:upper:]' '[:lower:]' <<<$HOST_OS_TYPE)"
if [[ "$#" -ge 1 ]]; then
	BUILD_TARGET="$1"
fi
export BUILD_TARGET

BUILD_TARGET_DYNLIB_EXT="so"
if [[ "${BUILD_TARGET}" == "windows" ]]; then
	BUILD_TARGET_DYNLIB_EXT="dll"
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
	BUILD_TARGET_DYNLIB_EXT="dylib"
elif [[ "${BUILD_TARGET}" != "linux" ]]; then
	die "Unknown build target: ${BUILD_TARGET}"
fi
export BUILD_TARGET_DYNLIB_EXT

default_cpp_compiler_args=''
has_clang=1
clang --version || has_clang=0
if [[ $has_clang -ne 0 ]]; then
	default_cc="clang"
	default_cpp_compiler="clang++"
else
	default_cc="gcc"
	default_cpp_compiler="g++"
fi
if [[ "${BUILD_TARGET}" == "windows" ]]; then
	default_cc="x86_64-w64-mingw32-gcc"
	default_cpp_compiler="x86_64-w64-mingw32-g++"
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
	default_cpp_compiler_args="-std=c++11"
fi
CC="${CC:-$default_cc}"
CPP_COMPILER="${CPP_COMPILER:-$default_cpp_compiler}"
CPP_COMPILER_ARGS="${CPP_COMPILER_ARGS:-$default_cpp_compiler_args}"
export CC
export CPP_COMPILER
export CPP_COMPILER_ARGS

if [[ "${NUM_CPUS:-}" != "" ]]; then
	echo "keeping NUM_CPUS which is already set to ${NUM_CPUS}"
else
	if [[ "${HOST_OS_TYPE}" == "MacOS" ]]; then
		NUM_CPUS=$(sysctl -n hw.ncpu)
	else
		NUM_CPUS=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
	fi
	echo "NUM_CPUS detected as ${NUM_CPUS}"
fi

CMAKE_TOOLCHAIN_FILE=""
if [[ "${CMAKE_TOOLCHAIN_FILE:-}" != "" ]]; then
	echo "Using user-provided CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}"
elif [[ "${BUILD_TARGET}" == "linux" ]]; then
	if [[ "$CC" == "clang" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-generic_linux-linux-x86_64-clang.cmake"
	elif [[ "$CC" == "gcc" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-generic_linux-linux-x86_64-gcc.cmake"
	fi
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
	if [[ "${HOST_OS_NAME} ${HOST_OS_VERSION}" == "Ubuntu jammy" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-ubuntu22.04-windows-x86_64-mingw.cmake"
	elif [[ "${HOST_OS_NAME}" == "Windows" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-windows_github-windows-x86_64-msvc_clang.cmake"
	else
		die "Missing CMake toolchain for ${HOST_OS_NAME} ${HOST_OS_VERSION} (target: ${BUILD_TARGET})"
	fi
fi

if [[ "${HOST_OS_TYPE}" == "MacOS" ]]; then
	cmake_app_bin_path="/Applications/CMake.app/Contents/bin"
	if which cmake; then
		echo "CMake binary found on PATH already..."
	elif [[ -d "${cmake_app_bin_path}" ]]; then
		echo "CMake binary does not exist on PATH, extending by ${cmake_app_bin_path}"
		export PATH="${cmake_app_bin_path}:${PATH}"
	else
		echo "WARNING: CMake binary not found on PATH and also not in ${cmake_bin_path}"
	fi
fi

# normalize paths
if [[ "${CMAKE_TOOLCHAIN_FILE}" != "" ]]; then
	CMAKE_TOOLCHAIN_FILE=$(realpath "${CMAKE_TOOLCHAIN_FILE}")
fi

echo
echo "Build target: ${BUILD_TARGET}"
if [[ "${CMAKE_TOOLCHAIN_FILE}" != "" ]]; then
	echo "  applying CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}"
	export CMAKE_TOOLCHAIN_FILE
else
	echo "  no CMake toolchain"
fi
echo "  CC:                $CC"
echo "  CPP_COMPILER:      $CPP_COMPILER"
echo "  CPP_COMPILER_ARGS: $CPP_COMPILER_ARGS"
echo
