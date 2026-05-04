# CMake toolchain file for cross-compiling to ARM64 Linux (e.g. Nvidia Jetson).
# Usage: cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake

set(CMAKE_SYSTEM_NAME       Linux)
set(CMAKE_SYSTEM_PROCESSOR  aarch64)

# Tell CMake the multiarch suffix so find_library descends into
# /usr/lib/aarch64-linux-gnu/ on a Debian/Ubuntu multiarch host.
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Search both:
#   - the toolchain's own sysroot at /usr/aarch64-linux-gnu (for traditional
#     cross builds on hosts without multiarch), and
#   - the host filesystem rooted at / (for Debian/Ubuntu multiarch where
#     libssl-dev:arm64 lands under /usr/lib/aarch64-linux-gnu/).
set(CMAKE_FIND_ROOT_PATH
    /usr/aarch64-linux-gnu
    /
)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# pkg-config for arm64 multiarch.
set(ENV{PKG_CONFIG_PATH}   "/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig")

# OpenSSL hint for FindOpenSSL.cmake when the multiarch headers/libs are
# installed via libssl-dev:arm64. Harmless if a real sysroot is used instead.
set(OPENSSL_ROOT_DIR     /usr/lib/aarch64-linux-gnu)
set(OPENSSL_INCLUDE_DIR  /usr/include)
set(OPENSSL_CRYPTO_LIBRARY /usr/lib/aarch64-linux-gnu/libcrypto.so)
set(OPENSSL_SSL_LIBRARY    /usr/lib/aarch64-linux-gnu/libssl.so)
