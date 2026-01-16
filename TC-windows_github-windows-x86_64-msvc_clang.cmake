# Host system: Windows (Server 2022, GitHub)
# Target OS:   Windows
# Target CPU:  x86, 64 Bit
# Compiler:    Visual Studio 17 2022 Enterprise Edition with Clang 19
#              License provided by GitHub: https://github.com/actions/runner-images/issues/12945
#              "We use an Enterprise license key, which, as agreed with Microsoft, is the version
#              for public and private use on GitHub Actions and Azure DevOps Hosted Runners.
#              Downgrading to the Community version is not practical. Users do not need to maintain
#              a separate personal copy of the license."

set(CMAKE_GENERATOR_TOOLSET ClangCL)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER   clang.exe)
set(CMAKE_CXX_COMPILER clang++.exe)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 10.0.26100.0)
set(CMAKE_C_STANDARD 11)
