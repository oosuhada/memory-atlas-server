# vcpkg triplet for ARM64 Linux (ECS optimized)
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Optimization flags for ARM64 ECS environment
set(VCPKG_C_FLAGS "-march=armv8-a -mtune=generic -O3 -fPIC")
set(VCPKG_CXX_FLAGS "-march=armv8-a -mtune=generic -O3 -fPIC -std=c++20")

# Use Release builds for better performance
set(VCPKG_BUILD_TYPE release)

# Disable debug symbols to reduce image size
set(VCPKG_RELEASE_FLAGS "-O3 -DNDEBUG")

# Platform-specific settings
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
)
