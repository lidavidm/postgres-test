# Copyright (c) 2026 ADBC Drivers Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import math
import subprocess
import tempfile
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scale-factor", type=float, default=2)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.scale_factor) or args.scale_factor <= 0:
        parser.error("scale factor must be finite and positive")
    output = args.output or Path(f"data/sf{args.scale_factor:g}/lineitem.arrow")
    output.parent.mkdir(parents=True, exist_ok=True)
    names = [
        "l_orderkey",
        "l_partkey",
        "l_suppkey",
        "l_linenumber",
        "l_quantity",
        "l_extendedprice",
        "l_discount",
        "l_tax",
        "l_returnflag",
        "l_linestatus",
        "l_shipdate",
        "l_commitdate",
        "l_receiptdate",
        "l_shipinstruct",
        "l_shipmode",
        "l_comment",
    ]
    types = (
        [pa.int64()] * 3
        + [pa.int32()]
        + [pa.decimal128(15, 2)] * 4
        + [pa.string()] * 2
        + [pa.date32()] * 3
        + [pa.string()] * 3
    )
    schema = pa.schema(zip(names, types))
    partial = output.with_suffix(".arrow.partial")
    try:
        with tempfile.TemporaryDirectory(dir=output.parent) as directory:
            subprocess.run(
                [
                    "tpchgen-cli",
                    "parquet",
                    "--scale-factor",
                    str(args.scale_factor),
                    "--tables",
                    "lineitem",
                    "--output-dir",
                    directory,
                ],
                check=True,
            )
            with (
                pa.OSFile(str(partial), "wb") as sink,
                pa.ipc.new_stream(sink, schema) as writer,
            ):
                for file in sorted(Path(directory).glob("*.parquet")):
                    for batch in pq.ParquetFile(file).iter_batches():
                        writer.write_batch(batch.cast(schema))
        partial.replace(output)
    finally:
        partial.unlink(missing_ok=True)
    print(output)


if __name__ == "__main__":
    main()
