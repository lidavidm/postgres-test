# Copyright (c) 2026 Columnar Technologies Inc.  All rights reserved.
cmake_minimum_required(VERSION 4.0)
project(lineitem_benchmark LANGUAGES C CXX)
include(FetchContent)
set(ADBC_DEFINE_COMMON_ENTRYPOINTS ON CACHE BOOL "" FORCE)
set(ADBC_WITH_VENDORED_FMT ON CACHE BOOL "" FORCE)
set(ADBC_WITH_VENDORED_NANOARROW ON CACHE BOOL "" FORCE)
set(ADBC_DRIVER_POSTGRESQL ON CACHE BOOL "" FORCE)
set(ADBC_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(ADBC_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(ADBC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ADBC_DRIVER_MANAGER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(adbc
  GIT_REPOSITORY https://github.com/apache/arrow-adbc.git
  GIT_TAG f1f942533897f8a965e1910730310c08e3af47bd
  GIT_SUBMODULES ""
  SOURCE_SUBDIR c)
# Prefer the manifest installation over any host libpq installation.
set(saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
set(ENV{PKG_CONFIG_PATH}
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/pkgconfig")
FetchContent_MakeAvailable(adbc)
set(ENV{PKG_CONFIG_PATH} "${saved_pkg_config_path}")
add_executable(lineitem_benchmark main.cc)
target_compile_features(lineitem_benchmark PRIVATE cxx_std_20)
target_include_directories(lineitem_benchmark PRIVATE "${adbc_SOURCE_DIR}/c/include")
target_link_libraries(lineitem_benchmark PRIVATE adbc_driver_postgresql_shared)
