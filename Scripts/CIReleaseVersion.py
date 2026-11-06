"""Choose and apply the product version for the nightly release.

plan:  print `version=X.Y.Z`, `released=true|false` and `released_by=<tags>` (the vX.Y.Z tags that already release
       HEAD, comma-separated). Refuses a base commit whose ini already says bReleaseBuild=True.
apply: write ProjectVersion=X.Y.Z and [Lockdown.Build] bReleaseBuild=True into Config/DefaultGame.ini, changing only
       those two values. The title screen shows the version only with that flag; every other build shows its commit.

Version rule: when the ini version has no tag yet and is newer than every vX.Y.Z tag (a hand-edited minor/major bump
merged to main), release it as is. Otherwise release the patch after the newer of the ini version and the latest tag.

Every release gets a version commit (it at least sets the flag), and it is never pushed to main: the release tag
points at it and its parent is the main commit it releases. So HEAD is released when a vX.Y.Z tag's commit has HEAD
as its first parent, or is HEAD itself (a tag made by hand on a main commit). The check reads only the tag commits themselves, so it works in the shallow CI workspace, where the tags
are fetched at depth 1 and `git tag --contains` cannot see their history.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INI = Path("Config", "DefaultGame.ini")
SECTION = b"/Script/EngineSettings.GeneralProjectSettings"
BUILD_SECTION = b"Lockdown.Build"
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


def _value_span(raw, section, key):
    """Return the (start, end) byte span of the single `key` value in [section]."""
    spans = []
    in_section = False
    offset = 0
    pattern = re.compile(rb"(\s*" + re.escape(key) + rb"\s*=\s*)(\S*)")
    for line in raw.splitlines(keepends=True):
        text = line.strip()
        header = re.fullmatch(rb"\[(.+)\]", text)
        if header:
            in_section = header.group(1) == section
        elif in_section:
            match = pattern.match(line)
            if match:
                spans.append((offset + match.end(1), offset + match.end(2)))
        offset += len(line)
    if len(spans) != 1:
        raise ValueError(f"expected exactly one {key.decode()} in [{section.decode()}] of {INI}, found {len(spans)}")
    return spans[0]


def _replace(raw, span, value):
    start, end = span
    return raw[:start] + value.encode("ascii") + raw[end:]


def read_project_version(raw):
    start, end = _value_span(raw, SECTION, b"ProjectVersion")
    version = raw[start:end].decode("ascii")
    parse_version(version)
    return version


def set_project_version(raw, version):
    parse_version(version)
    return _replace(raw, _value_span(raw, SECTION, b"ProjectVersion"), version)


def read_release_build(raw):
    start, end = _value_span(raw, BUILD_SECTION, b"bReleaseBuild")
    value = raw[start:end].decode("ascii")
    if value not in ("True", "False"):
        raise ValueError(f"bReleaseBuild in [{BUILD_SECTION.decode()}] of {INI} must be True or False: {value!r}")
    return value == "True"


def set_release_build(raw):
    read_release_build(raw)
    return _replace(raw, _value_span(raw, BUILD_SECTION, b"bReleaseBuild"), "True")


def _git(root, *args):
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.splitlines()


def _first_parent(root, commit):
    # The raw commit header: a shallow commit's parent line is still there, though git treats it as parentless.
    for line in _git(root, "cat-file", "commit", commit):
        if not line:
            return None
        if line.startswith("parent "):
            return line.split()[1]
    return None


def released_by(root=ROOT):
    """Return the vX.Y.Z tags whose commit is HEAD or has HEAD as its first parent."""
    head = _git(root, "rev-parse", "--verify", "HEAD")[0]
    tags = []
    for tag in _git(root, "tag", "--list", "v*"):
        if not TAG.match(tag):
            continue
        commit = _git(root, "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}")[0]
        if head in (commit, _first_parent(root, commit)):
            tags.append(tag)
    return tags


def plan(root=ROOT):
    raw = (Path(root) / INI).read_bytes()
    if read_release_build(raw):
        raise ValueError(f"main's {INI} has bReleaseBuild=True; only the nightly's version commit may set it")
    version, _ = next_version(read_project_version(raw), _git(root, "tag", "--list", "v*"))
    tags = released_by(root)
    return {"version": version, "released": bool(tags), "released_by": ",".join(tags)}


def apply(version, root=ROOT):
    path = Path(root) / INI
    raw = path.read_bytes()
    path.write_bytes(set_release_build(set_project_version(raw, version)))


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
            print(f"ProjectVersion={args.version} bReleaseBuild=True")
    except (ValueError, RuntimeError, OSError) as error:
        print(f"CIReleaseVersion: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
