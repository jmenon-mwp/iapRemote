cmake_minimum_required(VERSION 3.10)
project(iap-remote)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure PkgConfig to find vcpkg libraries
set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/opt/vcpkg/installed/x64-linux/lib/pkgconfig")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(GCP_IAP REQUIRED google_cloud_cpp_iap)

include_directories(
    ${GTKMM_INCLUDE_DIRS}
    ${GCP_IAP_INCLUDE_DIRS}
)

link_directories(
    ${GTKMM_LIBRARY_DIRS}
    ${GCP_IAP_LIBRARY_DIRS}
)

add_executable(iap-remote main.cpp)
target_link_libraries(iap-remote 
    ${GTKMM_LIBRARIES}
    ${GCP_IAP_LIBRARIES}
)

# Simple install rule
install(TARGETS iap-remote DESTINATION bin)
install(FILES styles.css DESTINATION bin)
