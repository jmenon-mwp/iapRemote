cmake_minimum_required(VERSION 3.10)
project(iapRemote)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure PkgConfig to find vcpkg libraries if on Linux
if(UNIX AND NOT APPLE)
    set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/opt/vcpkg/installed/x64-linux/lib/pkgconfig")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(VTE REQUIRED vte-2.91)
pkg_check_modules(GCP_IAP REQUIRED google_cloud_cpp_iap)

include_directories(
    ${GTKMM_INCLUDE_DIRS}
    ${VTE_INCLUDE_DIRS}
    ${GCP_IAP_INCLUDE_DIRS}
)

add_executable(iapRemote 
    main.cpp 
    Connections.cpp 
    Connections.h
)

target_link_libraries(iapRemote
    PRIVATE
    ${GTKMM_LIBRARIES}
    ${VTE_LIBRARIES}
    ${GCP_IAP_LIBRARIES}
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
