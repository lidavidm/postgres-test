<!--
Copyright (c) 2026 ADBC Drivers Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# pgrust lineitem sandbox

Requires Docker Compose and uv. Run:

```sh
uv run python load_lineitem.py
```

The script starts pgrust, generates SF=2 lineitem (roughly 12 million rows,
1.5 GB of CSV), and loads `public.lineitem_heap` and
`public.lineitem_columnar`. Both use identical data. SF measures the entire
TPC-H dataset; lineitem is only part of it. Allow several GB of free disk
space for CSV, both tables, and database logs.

The host writes `data/sf2/lineitem.csv`. Compose mounts `data/` read-only at
`/data`, allowing server-side COPY without sending the data through Python.
Database storage persists in a separate named volume.

```sh
uv run python load_lineitem.py --scale-factor 0.001
docker compose exec pgrust psql -U postgres -d tpch
```

Every run drops and reloads the two lineitem tables transactionally.
CSV is reused when `data/sf<SCALE>/.stamp` and `lineitem.csv` both exist.
The stamp is written only after successful generation. Delete the stamp to
force regeneration.
The script prints SQL timings, counts, and sizes, then analyzes both tables.
Host connections use `localhost:5432`, database `tpch`, and user/password
`postgres`/`postgres` (local development credentials).

```sh
docker compose stop
docker compose down
```

Both preserve database data. `docker compose down -v` deletes the database;
remove generated `data/` separately when no longer needed.

If port 5432 is occupied, prefix commands with `PGRUST_PORT=55432` to
publish that host port instead.

## C++ ADBC benchmark

Requires CMake 4+, Ninja, a C++20 compiler, pkg-config, and vcpkg's Linux
build prerequisites (including curl, zip, unzip, tar, and autoconf).

```sh
git submodule update --init --recursive
./vcpkg/bootstrap-vcpkg.sh
cmake --preset release -S cpp
cmake --build build
./build/lineitem_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch'
```

The benchmark assumes the loader has populated both tables. For each table
it runs seven scans with default COPY behavior, then seven with COPY disabled.
It discards the first two timings per mode and reports minimum, maximum,
median, and arithmetic mean seconds for the remaining five. Timing covers
query execution and consumption/release of all Arrow batches, excluding
connection and statement setup. Values are not individually decoded by the
benchmark. Row counts must agree across all scans of each table.

## Bulk ingestion benchmark

```sh
uv run python generate_lineitem_arrow.py --scale-factor 1
cmake --preset release -S cpp
cmake --build build
./build/lineitem_ingest_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch' data/sf1/lineitem.arrow
```

Generation converts temporary Parquet to an uncompressed Arrow IPC stream,
normalizing types to the SQL lineitem schema, and removes the Parquet.
The executable decodes the entire file once with nanoarrow; decoded data must
fit in memory. Each run binds a fresh stream over those in-memory batches.
Seven ingestions per storage method discard two warmups and report statistics
for five measured runs. Timing covers ADBC bulk-ingest execution in Append
mode, excluding file reading/decoding, binding, and table drop/create.
Bulk ingestion uses COPY internally. This benchmark does not toggle use_copy.
It drops and recreates `public.lineitem_ingest_heap` and
`public.lineitem_ingest_columnar`, leaving the final contents for inspection.
The query benchmark's original tables are preserved.

## Direct dependency audit (2026-11-12)

Repository-managed dependencies were checked against upstream release metadata:

| Dependency | Selected release | Status |
| --- | --- | --- |
| [ADBC](https://github.com/apache/arrow-adbc/releases) | 25 (C/C++ 1.13.0) | Latest release |
| [nanoarrow](https://github.com/apache/arrow-nanoarrow/releases) | 0.9.0 | Latest release; replaced development commit |
| [vcpkg](https://github.com/microsoft/vcpkg/releases) | 2026.10.16 | Latest tagged release; baseline matches submodule |
| [libpq](https://www.postgresql.org/support/versioning/) | 18.4 (vcpkg port revision 1) | Latest upstream is 18.6; retain registry version without an overlay |
| [PyArrow](https://pypi.org/project/pyarrow/) | 26.0.1 | Latest release |
| [tpchgen-cli](https://pypi.org/project/tpchgen-cli/) | 4.0.0 | Latest release |
| [pgrust](https://github.com/malisper/pgrust/releases) | v0.4-beta | Latest published release |
| [Ruff hooks](https://github.com/astral-sh/ruff-pre-commit/releases) | 0.17.4 | Latest release |
| [codespell](https://github.com/codespell-project/codespell/releases) | 2.4.3 | Latest release |
| [pre-commit-hooks](https://github.com/pre-commit/pre-commit-hooks/releases) | 6.0.0 | Latest release |
| [clang-format hooks](https://github.com/pre-commit/mirrors-clang-format/tags) | 23.1.5 | Latest release tag |

CMake 4+ remains the minimum supported build-tool version. Host tools are
provided by the environment; transitive dependencies follow the pinned upstream
projects and vcpkg baseline.
