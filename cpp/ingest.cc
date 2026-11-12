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
#include <nanoarrow/nanoarrow_ipc.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "common.h"
struct Data {
  ArrowSchema schema{};
  std::vector<ArrowArray> arrays;
  ~Data() {
    for (auto& a : arrays)
      if (a.release) a.release(&a);
    if (schema.release) schema.release(&schema);
  }
};
struct Replay {
  std::shared_ptr<Data> data;
  size_t index = 0;
};
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: lineitem_ingest_benchmark URI ARROW_PATH\n";
    return argc == 2 && std::string(argv[1]) == "--help" ? 0 : 1;
  }
  try {
    auto data = std::make_shared<Data>();
    FILE* file = fopen(argv[2], "rb");
    if (!file) throw std::runtime_error("open Arrow input");
    ArrowIpcInputStream input{};
    ArrowIpcInputStreamInitFile(&input, file, 1);
    Stream source;
    if (ArrowIpcArrayStreamReaderInit(&source.value, &input, nullptr))
      throw std::runtime_error("initialize IPC reader");
    if (source.value.get_schema(&source.value, &data->schema))
      throw std::runtime_error("read schema");
    char const* names[] = {
        "l_orderkey",    "l_partkey",       "l_suppkey",  "l_linenumber",
        "l_quantity",    "l_extendedprice", "l_discount", "l_tax",
        "l_returnflag",  "l_linestatus",    "l_shipdate", "l_commitdate",
        "l_receiptdate", "l_shipinstruct",  "l_shipmode", "l_comment"};
    char const* formats[] = {"l",      "l",      "l", "i", "d:15,2", "d:15,2",
                             "d:15,2", "d:15,2", "u", "u", "tdD",    "tdD",
                             "tdD",    "u",      "u", "u"};
    if (data->schema.n_children != 16)
      throw std::runtime_error("expected 16 lineitem columns");
    for (int i = 0; i < 16; ++i) {
      auto* field = data->schema.children[i];
      if (!field->name || std::string(field->name) != names[i] ||
          !field->format || std::string(field->format) != formats[i])
        throw std::runtime_error(std::string("incompatible lineitem column: ") +
                                 names[i]);
    }
    int64_t rows = 0;
    while (true) {
      ArrowArray a{};
      if (source.value.get_next(&source.value, &a))
        throw std::runtime_error("read IPC batch");
      if (!a.release) break;
      rows += a.length;
      data->arrays.push_back(a);
    }
    Error e;
    Database db;
    e.Check(AdbcDatabaseNew(&db.value, &e.value), "create database");
    e.Check(AdbcDatabaseSetOption(&db.value, "uri", argv[1], &e.value),
            "set URI");
    e.Check(AdbcDatabaseInit(&db.value, &e.value), "connect");
    Connection cn;
    e.Check(AdbcConnectionNew(&cn.value, &e.value), "create connection");
    e.Check(AdbcConnectionInit(&cn.value, &db.value, &e.value),
            "initialize connection");
    auto sql = [&](std::string q) {
      Statement s;
      e.Check(AdbcStatementNew(&cn.value, &s.value, &e.value), "statement");
      e.Check(AdbcStatementSetSqlQuery(&s.value, q.c_str(), &e.value), "SQL");
      e.Check(AdbcStatementExecuteQuery(&s.value, nullptr, nullptr, &e.value),
              "execute SQL");
    };
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
    bool const pgrust = identity.find("pgrust") != std::string::npos;
    std::cerr << "Server: " << version << "; columnar=" << pgrust << '\n';
    info.reset();
    raw.reset();
    if (pgrust)
      sql("DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_am WHERE "
          "amname='pgrcolumnar') THEN CREATE ACCESS METHOD pgrcolumnar TYPE "
          "TABLE HANDLER heap_tableam_handler; END IF; END $$;");
    for (bool columnar : {false, true}) {
      if (columnar && !pgrust) continue;
      std::string table =
          columnar ? "lineitem_ingest_columnar" : "lineitem_ingest_heap";
      std::array<double, 5> times{};
      for (int run = 0; run < 7; run++) {
        sql("DROP TABLE IF EXISTS public." + table);
        sql("CREATE TABLE public." + table +
            " (l_orderkey bigint,l_partkey bigint,l_suppkey "
            "bigint,l_linenumber integer,l_quantity "
            "numeric(15,2),l_extendedprice numeric(15,2),l_discount "
            "numeric(15,2),l_tax numeric(15,2),l_returnflag text,l_linestatus "
            "text,l_shipdate date,l_commitdate date,l_receiptdate "
            "date,l_shipinstruct text,l_shipmode text,l_comment text) USING " +
            (columnar ? "pgrcolumnar" : "heap"));
        Stream stream;
        stream.value.private_data = new Replay{data};
        stream.value.get_schema = [](ArrowArrayStream* s, ArrowSchema* out) {
          return ArrowSchemaDeepCopy(
              &static_cast<Replay*>(s->private_data)->data->schema, out);
        };
        stream.value.get_next = [](ArrowArrayStream* s, ArrowArray* out) {
          auto* r = static_cast<Replay*>(s->private_data);
          if (r->index == r->data->arrays.size()) {
            out->release = nullptr;
            return 0;
          }
          // Borrow the immutable buffers and child arrays. The root callback
          // retains their owner until the driver releases this batch.
          *out = r->data->arrays[r->index++];
          out->private_data = new std::shared_ptr<Data>(r->data);
          out->release = [](ArrowArray* a) {
            delete static_cast<std::shared_ptr<Data>*>(a->private_data);
            a->release = nullptr;
          };
          return 0;
        };
        stream.value.get_last_error = [](ArrowArrayStream*) -> char const* {
          return nullptr;
        };
        stream.value.release = [](ArrowArrayStream* s) {
          delete static_cast<Replay*>(s->private_data);
          s->release = nullptr;
        };
        Statement st;
        e.Check(AdbcStatementNew(&cn.value, &st.value, &e.value), "statement");
        for (auto pair :
             {std::pair{ADBC_INGEST_OPTION_TARGET_TABLE, table.c_str()},
              std::pair{ADBC_INGEST_OPTION_TARGET_DB_SCHEMA, "public"},
              std::pair{ADBC_INGEST_OPTION_MODE,
                        ADBC_INGEST_OPTION_MODE_APPEND}})
          e.Check(AdbcStatementSetOption(&st.value, pair.first, pair.second,
                                         &e.value),
                  "set ingest option");
        e.Check(AdbcStatementBindStream(&st.value, &stream.value, &e.value),
                "bind stream");
        int64_t affected = 0;
        auto start = std::chrono::steady_clock::now();
        e.Check(
            AdbcStatementExecuteQuery(&st.value, nullptr, &affected, &e.value),
            "ingest");
        double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start)
                             .count();
        if (affected != rows) throw std::runtime_error("row count mismatch");
        Statement check;
        e.Check(AdbcStatementNew(&cn.value, &check.value, &e.value),
                "create count statement");
        std::string count_sql = "SELECT count(*) FROM public." + table;
        e.Check(
            AdbcStatementSetSqlQuery(&check.value, count_sql.c_str(), &e.value),
            "set count query");
        Stream counts;
        e.Check(AdbcStatementExecuteQuery(&check.value, &counts.value, nullptr,
                                          &e.value),
                "count ingested rows");
        Batch count_batch;
        if (counts.value.get_next(&counts.value, &count_batch.value) ||
            !count_batch.value.release || count_batch.value.length != 1)
          throw std::runtime_error("read ingested row count");
        auto* values = count_batch.value.children[0];
        int64_t actual = 0;
        std::memcpy(&actual,
                    static_cast<char const*>(values->buffers[1]) +
                        values->offset * sizeof(int64_t),
                    sizeof(actual));
        if (actual != rows)
          throw std::runtime_error("stored row count mismatch");
        if (run >= 2) times[run - 2] = seconds;
      }
      Report(table, rows, times);
    }
  } catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
