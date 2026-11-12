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

#include "common.h"
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "Usage: lineitem_benchmark POSTGRESQL_URI\n";
    return 0;
  }
  if (argc != 2) {
    std::cerr << "Usage: lineitem_benchmark POSTGRESQL_URI\n";
    return 1;
  }
  try {
    Error error;
    Database database;
    error.Check(AdbcDatabaseNew(&database.value, &error.value),
                "create database");
    error.Check(
        AdbcDatabaseSetOption(&database.value, "uri", argv[1], &error.value),
        "set URI");
    error.Check(AdbcDatabaseInit(&database.value, &error.value),
                "connect database");
    Connection connection;
    error.Check(AdbcConnectionNew(&connection.value, &error.value),
                "create connection");
    error.Check(
        AdbcConnectionInit(&connection.value, &database.value, &error.value),
        "initialize connection");
    for (char const* table :
         {"public.lineitem_heap", "public.lineitem_columnar"}) {
      int64_t expected = -1;
      for (bool copy : {true, false}) {
        std::array<double, 5> times{};
        for (int run = 0; run < 7; ++run) {
          Statement statement;
          error.Check(AdbcStatementNew(&connection.value, &statement.value,
                                       &error.value),
                      "create statement");
          if (!copy)
            error.Check(AdbcStatementSetOption(&statement.value,
                                               "adbc.postgresql.use_copy",
                                               "false", &error.value),
                        "disable COPY");
          std::string const query = std::string("SELECT * FROM ") + table;
          error.Check(AdbcStatementSetSqlQuery(&statement.value, query.c_str(),
                                               &error.value),
                      "set query");
          Stream stream;
          int64_t rows = 0;
          auto const start = std::chrono::steady_clock::now();
          error.Check(AdbcStatementExecuteQuery(&statement.value, &stream.value,
                                                nullptr, &error.value),
                      "execute query");
          while (true) {
            Batch batch;
            if (stream.value.get_next(&stream.value, &batch.value)) {
              char const* message = stream.value.get_last_error(&stream.value);
              throw std::runtime_error(message ? message : "read batch failed");
            }
            if (!batch.value.release) break;
            rows += batch.value.length;
          }
          double const seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
          if (expected >= 0 && rows != expected)
            throw std::runtime_error("inconsistent row counts");
          expected = rows;
          if (run >= 2) times[run - 2] = seconds;
        }
        Report(std::string(table) + " copy=" + (copy ? "default" : "disabled"),
               expected, times);
      }
    }
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
