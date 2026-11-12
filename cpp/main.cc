// Copyright (c) 2026 Columnar Technologies Inc.  All rights reserved.
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
        std::sort(times.begin(), times.end());
        std::cout << table << " copy=" << (copy ? "default" : "disabled")
                  << " rows=" << expected << " runs=7 discarded=2 measured=5"
                  << " min_s=" << times.front() << " max_s=" << times.back()
                  << " median_s=" << times[2] << " average_s="
                  << std::accumulate(times.begin(), times.end(), 0.0) /
                         times.size()
                  << std::endl;
      }
    }
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
