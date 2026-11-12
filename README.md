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
# Start pgrust
docker compose up
# Load tables
uv run python load_lineitem.py
# Generate data for ingest
uv run python generate_lineitem_arrow.py --scale-factor 1 --output $(pwd)/data/sf1/lineitem.arrow
```

`load_lineitem.py`, generates lineitem SF=2 (~12 million rows, 1.5 GB
of CSV), and creates `public.lineitem_heap` and `public.lineitem_columnar`.

## Query benchmark

```sh
./build/lineitem_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch'
```

For each table it runs seven scans with COPY, then seven with COPY disabled.
It discards the first two timings per mode and reports minimum, maximum,
median, and arithmetic mean seconds for the remaining five. Timing covers
query execution and consumption/release of all Arrow batches, excluding
connection and statement setup. Values are not individually decoded by the
benchmark. Row counts must agree across all scans of each table.

## Bulk ingestion benchmark

```sh
./build/lineitem_ingest_benchmark 'postgresql://postgres:postgres@localhost:5432/tpch' data/sf1/lineitem.arrow
```

The executable decodes the entire file once with nanoarrow; decoded data must
fit in memory. Each run binds a fresh stream over those in-memory batches.
Seven ingestions per storage method discard two warmups and report statistics
for five measured runs. Timing covers ADBC bulk-ingest, excluding file
reading/decoding, binding, and table drop/create. It drops and recreates
`public.lineitem_ingest_heap` and `public.lineitem_ingest_columnar`.
