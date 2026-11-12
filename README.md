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
