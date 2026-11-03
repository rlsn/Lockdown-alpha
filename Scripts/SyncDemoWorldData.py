#!/usr/bin/env python3
"""Generate the sole Demo polygon world from approved geometry and central content references."""
import argparse
from pathlib import Path
import sys

import DemoContent as content
from DemoWorld import compile_world
from SyncDemoContentData import _check_recovery_records, write_outputs


def build_outputs(root=content.ROOT):
    return compile_world(root).outputs


def synchronize(root=content.ROOT, check=False):
    _check_recovery_records(root)
    compilation = compile_world(root)
    if check:
        content.require(not (Path(root) / content.DATA / ".compile.lock").exists(), "CompilerBusy", content.DATA,
                        "active or unresolved generation journal")
        for relative, expected in compilation.outputs.items():
            path = Path(root) / relative
            content.require(path.is_file() and path.read_bytes() == expected, "StaleWorldOutput", relative,
                            "missing or different bytes; regenerate through SyncDemoWorldData.py")
    else:
        write_outputs(root, compilation)
    return compilation


def main(argv=None, root=content.ROOT):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check source/projection consistency without writing")
    args = parser.parse_args(argv)
    try:
        result = synchronize(root, check=args.check)
    except (OSError, ValueError) as error:
        print(f"FAIL Demo world: {error}", file=sys.stderr)
        return 2
    print(f"PASS Demo world {'check' if args.check else 'generation'}: payload SHA256 {result.digest.hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
