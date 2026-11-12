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

#include <libpq-fe.h>

#include <cctype>
#include <memory>
#include <vector>

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
    std::unique_ptr<PGconn, decltype(&PQfinish)> raw(PQconnectdb(argv[1]),
                                                     PQfinish);
    if (!raw || PQstatus(raw.get()) != CONNECTION_OK)
      throw std::runtime_error(raw ? PQerrorMessage(raw.get())
                                   : "allocate libpq connection");
    std::unique_ptr<PGresult, decltype(&PQclear)> info(
        PQexec(raw.get(), "SELECT version()"), PQclear);
    if (!info || PQresultStatus(info.get()) != PGRES_TUPLES_OK ||
        PQntuples(info.get()) != 1 || PQgetisnull(info.get(), 0, 0))
      throw std::runtime_error("read server version: " +
                               std::string(PQerrorMessage(raw.get())));
    std::string version = PQgetvalue(info.get(), 0, 0);
    if (version.empty()) throw std::runtime_error("empty server version");
    std::string identity = version;
    std::transform(identity.begin(), identity.end(), identity.begin(),
                   [](unsigned char value) { return std::tolower(value); });
    bool const columnar = identity.find("pgrust") != std::string::npos;
    std::cerr << "Server: " << version << "; columnar=" << columnar << '\n';
    std::vector<char const*> tables{"public.lineitem_heap"};
    if (columnar) tables.push_back("public.lineitem_columnar");
    for (char const* table : tables) {
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
      std::array<double, 5> times{};
      std::string const query = std::string("COPY (SELECT * FROM ") + table +
                                ") TO STDOUT (FORMAT binary)";
      for (int run = 0; run < 7; ++run) {
        auto const start = std::chrono::steady_clock::now();
        std::unique_ptr<PGresult, decltype(&PQclear)> result(
            PQexec(raw.get(), query.c_str()), PQclear);
        if (!result || PQresultStatus(result.get()) != PGRES_COPY_OUT)
          throw std::runtime_error(PQerrorMessage(raw.get()));
        result.reset();
        while (true) {
          char* data = nullptr;
          int const size = PQgetCopyData(raw.get(), &data, 0);
          std::unique_ptr<char, decltype(&PQfreemem)> buffer(data, PQfreemem);
          if (size == -1) break;
          if (size <= 0) throw std::runtime_error(PQerrorMessage(raw.get()));
        }
        std::string count;
        std::string failure;
        while (auto* next = PQgetResult(raw.get())) {
          result.reset(next);
          if (PQresultStatus(result.get()) != PGRES_COMMAND_OK)
            failure = PQresultErrorMessage(result.get());
          else
            count = PQcmdTuples(result.get());
        }
        result.reset();
        double const seconds = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (!failure.empty()) throw std::runtime_error(failure);
        if (count.empty()) throw std::runtime_error("missing COPY row count");
        size_t consumed = 0;
        int64_t const rows = std::stoll(count, &consumed);
        if (consumed != count.size() || rows != expected)
          throw std::runtime_error("inconsistent COPY row count");
        if (run >= 2) times[run - 2] = seconds;
      }
      Report(std::string(table) + " copy=libpq-binary-unparsed", expected,
             times);
    }
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
