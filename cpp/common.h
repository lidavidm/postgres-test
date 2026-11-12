// Copyright (c) 2026 ADBC Drivers Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include <arrow-adbc/adbc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

struct Error {
  AdbcError value{};
  ~Error() {
    if (value.release) value.release(&value);
  }
  void Check(AdbcStatusCode code, char const* action) {
    if (code != ADBC_STATUS_OK)
      throw std::runtime_error(std::string(action) + ": " +
                               (value.message ? value.message : "ADBC error"));
  }
};
struct Database {
  AdbcDatabase value{};
  ~Database() {
    if (value.private_data) AdbcDatabaseRelease(&value, nullptr);
  }
};
struct Connection {
  AdbcConnection value{};
  ~Connection() {
    if (value.private_data) AdbcConnectionRelease(&value, nullptr);
  }
};
struct Statement {
  AdbcStatement value{};
  ~Statement() {
    if (value.private_data) AdbcStatementRelease(&value, nullptr);
  }
};
struct Stream {
  ArrowArrayStream value{};
  ~Stream() {
    if (value.release) value.release(&value);
  }
};
struct Batch {
  ArrowArray value{};
  ~Batch() {
    if (value.release) value.release(&value);
  }
};

inline void Report(std::string const& label, int64_t rows,
                   std::array<double, 5> times) {
  std::sort(times.begin(), times.end());
  std::cout << label << " rows=" << rows
            << " runs=7 discarded=2 measured=5 min_s=" << times.front()
            << " max_s=" << times.back() << " median_s=" << times[2]
            << " average_s="
            << std::accumulate(times.begin(), times.end(), 0.0) / times.size()
            << std::endl;
}
