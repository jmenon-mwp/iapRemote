cmake_minimum_required(VERSION 3.10)
project(iapRemote)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version definition for the about box
if(NOT APP_VERSION)
    set(APP_VERSION "1.0.0")
endif()
add_definitions(-DAPP_VERSION="${APP_VERSION}")

# Configure Search Paths for vcpkg if on Linux and not using a toolchain
if(UNIX AND NOT APPLE AND NOT CMAKE_TOOLCHAIN_FILE)
    if(EXISTS "/opt/vcpkg/installed/x64-linux")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};/opt/vcpkg/installed/x64-linux")
    endif()
endif()

# Find Google Cloud C++ IAP
find_package(google_cloud_cpp_iap REQUIRED)

# Find OpenSSL (Crypto)
find_package(OpenSSL REQUIRED)

# Find nlohmann_json
find_package(nlohmann_json REQUIRED)

# Find GTK and VTE (Still using PkgConfig as they are system libraries)
find_package(PkgConfig REQUIRED)
if(STATIC_BUILD)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
endif()

pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-3.0)
pkg_check_modules(VTE REQUIRED IMPORTED_TARGET vte-2.91)

add_executable(iapRemote
    main.cpp
    Connections.cpp
    Connections.h
)

target_link_libraries(iapRemote
    PRIVATE
    google-cloud-cpp::iap
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    PkgConfig::GTKMM
    PkgConfig::VTE
)

# Platform specific configurations
if(WIN32)
    target_compile_definitions(iapRemote PRIVATE IS_WINDOWS)
    target_link_libraries(iapRemote PRIVATE ws2_32) # Winsock for networking
elseif(APPLE)
    target_compile_definitions(iapRemote PRIVATE IS_MACOS)
else()
    target_compile_definitions(iapRemote PRIVATE IS_LINUX)
endif()

# Installation
install(TARGETS iapRemote DESTINATION bin)
