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

# PostgreSQL + Arrow Demo

Tested on Linux/amd64.

Requires:

- CMake
- A C++20 toolchain (compiler, and vcpkg's dependencies: curl, zip, unzip, tar, and autoconf)
- pkg-config (for libpq)
- Docker Compose
- uv

## Setup

Build the benchmark client:

```sh
git submodule update --init --recursive
./vcpkg/bootstrap-vcpkg.sh
cmake --preset release -S cpp
cmake --build build
```

Start pgrust and generate data:

```sh
# Start pgrust (or postgres)
docker compose up pgrust
# Load tables
uv run python load_lineitem.py
# Generate data for ingest
uv run python generate_lineitem_arrow.py --scale-factor 1 --output $(pwd)/data/sf1/lineitem.arrow
```

`load_lineitem.py`, generates lineitem SF=2 (~12 million rows, 1.5 GB
of CSV), and creates `public.lineitem_heap` and `public.lineitem_columnar`.

Plain PostgreSQL is available as an optional service on port 5433:

```sh
docker compose up -d --wait postgres
uv run python load_lineitem.py --service postgres
```

Set `POSTGRES_PORT` to change its host port. It uses a separate data volume.
The loader defaults to `--service pgrust`. Both the loader and query benchmark
inspect `SELECT version()` and use the columnar table only on pgrust; plain
PostgreSQL uses only `public.lineitem_heap`.

## Query benchmark

```sh
./build/lineitem_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch'
```

For each table it runs seven ADBC scans with COPY, seven with COPY disabled,
and seven raw libpq binary COPY scans.  It discards the first two timings per
mode and reports minimum, maximum, median, and arithmetic mean seconds for the
remaining five. Timing covers query execution and consumption/release of all
Arrow batches, excluding connection and statement setup. Values are not
individually decoded by the benchmark. The raw libpq configuration consumes
and frees COPY buffers without parsing their contents. Row counts must agree
across all scans of each table.

## Bulk ingestion benchmark

```sh
./build/lineitem_ingest_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch' $(pwd)/data/sf1/lineitem.arrow
```

The executable decodes the entire file once with nanoarrow; decoded data must
fit in memory. Each run binds a fresh stream over those in-memory batches.
Seven ingestions per storage method discard two warmups and report statistics
for five measured runs. Timing covers ADBC bulk-ingest, excluding file
reading/decoding, binding, and table drop/create. It drops and recreates
`public.lineitem_ingest_heap` and, only when the server version identifies
pgrust, `public.lineitem_ingest_columnar`. Plain PostgreSQL runs heap ingestion
only and does not register a columnar access method.

## Sample Runs

Against pgrust, SF=2:

```
./build/lineitem_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch'
Server: PostgreSQL 18.6 (pgrust 0.4-beta, build 20911cc) on x86_64-unknown-linux-gnu, 64-bit; columnar=1
public.lineitem_heap copy=default rows=11997996 runs=7 discarded=2 measured=5 min_s=14.5145 max_s=15.1232 median_s=14.9472 average_s=14.8856
public.lineitem_heap copy=disabled rows=11997996 runs=7 discarded=2 measured=5 min_s=18.2068 max_s=18.3739 median_s=18.299 average_s=18.3018
public.lineitem_heap copy=libpq-binary-unparsed rows=11997996 runs=7 discarded=2 measured=5 min_s=14.5315 max_s=14.8476 median_s=14.5555 average_s=14.6306
public.lineitem_columnar copy=default rows=11997996 runs=7 discarded=2 measured=5 min_s=14.1412 max_s=14.2633 median_s=14.1985 average_s=14.1996
public.lineitem_columnar copy=disabled rows=11997996 runs=7 discarded=2 measured=5 min_s=21.6009 max_s=21.9022 median_s=21.7768 average_s=21.7778
public.lineitem_columnar copy=libpq-binary-unparsed rows=11997996 runs=7 discarded=2 measured=5 min_s=13.8949 max_s=14.2579 median_s=14.1177 average_s=14.1023

./build/lineitem_ingest_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch' data/sf1/lineitem.arrow
lineitem_ingest_heap rows=6001215 runs=7 discarded=2 measured=5 min_s=12.7946 max_s=37.3255 median_s=18.1491 average_s=22.1818
lineitem_ingest_columnar rows=6001215 runs=7 discarded=2 measured=5 min_s=26.1824 max_s=35.9995 median_s=26.4203 average_s=28.3002
```

Against PostgreSQL, SF=1:

```
./build/lineitem_benchmark 'postgresql://postgres:postgres@localhost:5433/tpch'
Server: PostgreSQL 18.6 (Debian 18.6-1.pgdg13+2) on x86_64-pc-linux-gnu, compiled by gcc (Debian 14.2.0-19) 14.2.0, 64-bit; columnar=0
public.lineitem_heap copy=default rows=6001215 runs=7 discarded=2 measured=5 min_s=6.77132 max_s=7.02384 median_s=6.87035 average_s=6.87318
public.lineitem_heap copy=disabled rows=6001215 runs=7 discarded=2 measured=5 min_s=9.11205 max_s=9.15869 median_s=9.13739 average_s=9.13277
public.lineitem_heap copy=libpq-binary-unparsed rows=6001215 runs=7 discarded=2 measured=5 min_s=6.74496 max_s=6.79097 median_s=6.76483 average_s=6.768

./build/lineitem_ingest_benchmark 'postgresql://postgres:postgres@localhost:5433/tpch' data/sf1/lineitem.arrow
lineitem_ingest_heap rows=6001215 runs=7 discarded=2 measured=5 min_s=11.7971 max_s=23.9614 median_s=18.8819 average_s=17.8811
```
