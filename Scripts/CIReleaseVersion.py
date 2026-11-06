"""Choose and apply the product version for the nightly release.

plan:  print `version=X.Y.Z`, `bump=true|false` and `released=true|false` (a vX.Y.Z tag already contains HEAD).
apply: write ProjectVersion=X.Y.Z into Config/DefaultGame.ini, changing only that value.

Version rule: when the ini version has no tag yet and is newer than every vX.Y.Z tag (a hand-edited minor/major bump
merged to main), release it as is. Otherwise release the patch after the newer of the ini version and the latest tag.

The version commit is never pushed to main: the release tag points at it and its parent is the main commit it
releases, so a tag that contains HEAD means that main commit is already released.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INI = Path("Config", "DefaultGame.ini")
SECTION = b"/Script/EngineSettings.GeneralProjectSettings"
VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
TAG = re.compile(r"^v(\d+\.\d+\.\d+)$")


def parse_version(text):
    match = VERSION.match(text)
    if not match:
        raise ValueError(f"version must use major.minor.patch: {text!r}")
    return tuple(int(part) for part in match.groups())


def format_version(version):
    return ".".join(str(part) for part in version)


def tag_versions(tags):
    return {parse_version(match.group(1)) for tag in tags if (match := TAG.match(tag))}


def next_version(ini, tags):
    """Return (version, whether the ini needs to change) for the ini version string and the repository tag names."""
    current = parse_version(ini)
    tagged = tag_versions(tags)
    latest = max(tagged, default=None)
    if current not in tagged and (latest is None or current > latest):
        return format_version(current), False
    major, minor, patch = max(current, latest)
    return format_version((major, minor, patch + 1)), True


def _version_line(raw):
    """Return the (start, end) byte span of the single ProjectVersion value in GeneralProjectSettings."""
    spans = []
    in_section = False
    offset = 0
    for line in raw.splitlines(keepends=True):
        text = line.strip()
        header = re.fullmatch(rb"\[(.+)\]", text)
        if header:
            in_section = header.group(1) == SECTION
        elif in_section:
            match = re.match(rb"(\s*ProjectVersion\s*=\s*)(\S*)", line)
            if match:
                spans.append((offset + match.end(1), offset + match.end(2)))
        offset += len(line)
    if len(spans) != 1:
        raise ValueError(f"expected exactly one ProjectVersion in [{SECTION.decode()}] of {INI}, found {len(spans)}")
    return spans[0]


def read_project_version(raw):
    start, end = _version_line(raw)
    version = raw[start:end].decode("ascii")
    parse_version(version)
    return version


def set_project_version(raw, version):
    parse_version(version)
    start, end = _version_line(raw)
    return raw[:start] + version.encode("ascii") + raw[end:]


def _git(root, *args):
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.splitlines()


def plan(root=ROOT):
    ini = read_project_version((Path(root) / INI).read_bytes())
    version, bump = next_version(ini, _git(root, "tag", "--list", "v*"))
    released = bool(tag_versions(_git(root, "tag", "--list", "v*", "--contains", "HEAD")))
    return {"version": version, "bump": bump, "released": released}


def apply(version, root=ROOT):
    path = Path(root) / INI
    raw = path.read_bytes()
    path.write_bytes(set_project_version(raw, version))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("plan")
    apply_parser = commands.add_parser("apply")
    apply_parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        if args.command == "plan":
            for key, value in plan().items():
                print(f"{key}={str(value).lower() if type(value) is bool else value}")
        else:
            apply(args.version)
            print(f"ProjectVersion={args.version}")
    except (ValueError, RuntimeError, OSError) as error:
        print(f"CIReleaseVersion: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
