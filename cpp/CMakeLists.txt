# Copyright (c) 2026 ADBC Drivers Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 4.0)
project(lineitem_benchmark LANGUAGES C CXX)
include(FetchContent)
set(ADBC_DEFINE_COMMON_ENTRYPOINTS ON CACHE BOOL "" FORCE)
set(ADBC_WITH_VENDORED_FMT ON CACHE BOOL "" FORCE)
set(ADBC_WITH_VENDORED_NANOARROW OFF CACHE BOOL "" FORCE)
set(ADBC_DRIVER_POSTGRESQL ON CACHE BOOL "" FORCE)
set(ADBC_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(ADBC_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(ADBC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ADBC_DRIVER_MANAGER OFF CACHE BOOL "" FORCE)
set(NANOARROW_IPC ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_Declare(nanoarrow
  GIT_REPOSITORY https://github.com/apache/arrow-nanoarrow.git
  GIT_TAG 9fad80292360d0f3978264c11ba865ccc94020d8 # Apache Arrow nanoarrow 0.9.0
  OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(nanoarrow)
FetchContent_Declare(adbc
  GIT_REPOSITORY https://github.com/apache/arrow-adbc.git
  GIT_TAG 1f9f5540dbcb695c540dfcdafeea421c7d94adb1 # Apache Arrow ADBC 25
  GIT_SUBMODULES ""
  SOURCE_SUBDIR c)
# Prefer the manifest installation over any host libpq installation.
set(saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
set(ENV{PKG_CONFIG_PATH}
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/pkgconfig")
FetchContent_MakeAvailable(adbc)
set(ENV{PKG_CONFIG_PATH} "${saved_pkg_config_path}")
find_package(PostgreSQL REQUIRED)
add_executable(lineitem_benchmark main.cc)
target_compile_features(lineitem_benchmark PRIVATE cxx_std_20)
target_include_directories(lineitem_benchmark PRIVATE "${adbc_SOURCE_DIR}/c/include")
target_link_libraries(lineitem_benchmark PRIVATE adbc_driver_postgresql_shared PostgreSQL::PostgreSQL)

add_executable(lineitem_ingest_benchmark ingest.cc)
target_compile_features(lineitem_ingest_benchmark PRIVATE cxx_std_20)
target_include_directories(lineitem_ingest_benchmark PRIVATE "${adbc_SOURCE_DIR}/c/include")
target_link_libraries(lineitem_ingest_benchmark PRIVATE adbc_driver_postgresql_shared nanoarrow::nanoarrow_ipc PostgreSQL::PostgreSQL)
