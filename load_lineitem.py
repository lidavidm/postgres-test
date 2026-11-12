# Copyright (c) 2026 Columnar Technologies Inc.  All rights reserved.
"""Generate TPC-H lineitem and COPY it into heap and columnar tables."""

import argparse
import math
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
COLUMNS = """
    l_orderkey bigint, l_partkey bigint, l_suppkey bigint,
    l_linenumber integer, l_quantity numeric(15,2),
    l_extendedprice numeric(15,2), l_discount numeric(15,2),
    l_tax numeric(15,2), l_returnflag text, l_linestatus text,
    l_shipdate date, l_commitdate date, l_receiptdate date,
    l_shipinstruct text, l_shipmode text, l_comment text
"""


def sql(statement):
    return subprocess.run(
        [
            "docker",
            "compose",
            "exec",
            "-T",
            "pgrust",
            "psql",
            "-X",
            "-U",
            "postgres",
            "-d",
            "tpch",
            "-v",
            "ON_ERROR_STOP=1",
        ],
        input=statement,
        text=True,
        check=True,
        cwd=ROOT,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scale-factor", type=float, default=2)
    args = parser.parse_args()
    if not math.isfinite(args.scale_factor) or args.scale_factor <= 0:
        parser.error("scale factor must be finite and positive")
    scale = format(args.scale_factor, ".15g")
    directory = ROOT / "data" / f"sf{scale}"
    directory.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["docker", "compose", "up", "-d", "--wait", "pgrust"], check=True, cwd=ROOT
    )
    filename = directory / "lineitem.csv"
    stamp = directory / ".stamp"
    if not (stamp.is_file() and filename.is_file()):
        stamp.unlink(missing_ok=True)
        subprocess.run(
            [
                "tpchgen-cli",
                "csv",
                "--scale-factor",
                scale,
                "--tables",
                "lineitem",
                "--output-dir",
                str(directory),
                "--overwrite",
            ],
            check=True,
        )
        filename.chmod(0o644)
        stamp.touch()
    else:
        print(f"Reusing {filename}", flush=True)
    filename.chmod(0o644)
    statements = [
        "\\timing on",
        "BEGIN;",
        """DO $$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_am WHERE amname = 'pgrcolumnar') THEN
      CREATE ACCESS METHOD pgrcolumnar TYPE TABLE HANDLER heap_tableam_handler;
    END IF; END $$;""",
    ]
    for name, method in [
        ("lineitem_heap", "heap"),
        ("lineitem_columnar", "pgrcolumnar"),
    ]:
        statements.append(f"DROP TABLE IF EXISTS public.{name};")
        statements.extend(
            [
                f"CREATE TABLE public.{name} ({COLUMNS}) USING {method};",
                (
                    f"COPY public.{name} FROM '/data/sf{scale}/lineitem.csv' "
                    "WITH (FORMAT csv, HEADER true);"
                ),
            ]
        )
    statements.append("COMMIT;")
    for name in ["lineitem_heap", "lineitem_columnar"]:
        statements.extend(
            [
                f"ANALYZE public.{name};",
                (
                    f"SELECT '{name}' AS name, count(*) AS rows, "
                    f"pg_size_pretty(pg_total_relation_size('public.{name}')) AS size "
                    f"FROM public.{name};"
                ),
            ]
        )
    sql("\n".join(statements))


if __name__ == "__main__":
    main()
