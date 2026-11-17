"""Plan small local checks from dependencies; CI remains the complete gate."""

import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

from CIChangeScope import INCLUDE, changed_paths
from CheckRuleReport import REGISTRATION

ROOT = Path(__file__).resolve().parents[1]
MAX_PYTHON_MODULES = 12
MAX_RULE_TESTS = 80
DATA_ROOTS = ("SourceData/", "Content/Lockdown/Data/")


def closure(graph, roots):
    found, pending = set(), list(roots)
    while pending:
        key = pending.pop()
        if key not in found:
            found.add(key)
            pending.extend(graph.get(key, ()))
    return found


class Dependencies:
    """Static imports, literal data paths and project includes, without importing test code."""

    def __init__(self, root):
        self.root = root
        self.python = {}
        self.reads = {}
        self.native = {}
        self.registrations = {}
        scripts = root / "Scripts"
        for path in sorted(scripts.glob("*.py")):
            key = path.relative_to(root).as_posix()
            tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=key)
            imports = set()
            reads = set()
            values = {"ROOT": "", "root": "", "SCRIPTS": "Scripts"}
            parents = {child: node for node in ast.walk(tree) for child in ast.iter_child_nodes(node)}

            def value(node):
                if isinstance(node, ast.Constant) and isinstance(node.value, str):
                    return node.value
                if isinstance(node, ast.Name):
                    return values.get(node.id)
                if isinstance(node, ast.Attribute) and node.attr in ("ROOT", "root"):
                    return ""
                if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Div, ast.Add)):
                    left, right = value(node.left), value(node.right)
                    if left is not None and right is not None:
                        return f"{left}/{right}" if isinstance(node.op, ast.Div) and left else left + right
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "Path":
                    parts = [value(arg) for arg in node.args]
                    if parts and all(part is not None for part in parts):
                        return "/".join(parts)
                return None

            for node in tree.body:
                if isinstance(node, ast.Assign):
                    text = value(node.value)
                    if text is not None:
                        for target in node.targets:
                            if isinstance(target, ast.Name):
                                values[target.id] = text
            for node in ast.walk(tree):
                names = []
                if isinstance(node, ast.Import):
                    names = [alias.name.split(".")[0] for alias in node.names]
                elif isinstance(node, ast.ImportFrom) and node.module:
                    names = [node.module.split(".")[0]]
                for name in names:
                    # Preserve missing local import edges for deleted/renamed modules.
                    if (scripts / f"{name}.py").is_file() or name[:1].isupper():
                        imports.add(f"Scripts/{name}.py")
                text = value(node)
                if text is None or "\n" in text:
                    continue
                parent = parents.get(node)
                if ((isinstance(parent, ast.BinOp) and isinstance(parent.op, (ast.Div, ast.Add)))
                        or (isinstance(parent, ast.Call) and isinstance(parent.func, ast.Name)
                            and parent.func.id == "Path")) and value(parent) is not None:
                    continue
                text = text.replace("\\", "/").removeprefix("./").rstrip("/")
                if text.split("/")[0] in ("SourceData", "Content", "Config", "Source", "Scripts"):
                    if text != "Scripts" and ".." not in text.split("/") and not re.search(r"[*?{}<>]", text):
                        reads.add(text)
                if re.fullmatch(r"[A-Za-z_]\w*(?:\.py|\.ps1)?", text):
                    name = text.removesuffix(".py")
                    if text.endswith(".py") or (scripts / f"{name}.py").is_file():
                        imports.add(f"Scripts/{name}.py")
                    elif text.endswith(".ps1"):
                        reads.add(f"Scripts/{text}")
            self.python[key] = imports
            self.reads[key] = reads

        source = root / "Source"
        files = sorted(p for p in source.rglob("*") if p.suffix in (".h", ".cpp", ".cs"))
        bases = [m / part for m in source.iterdir() if m.is_dir()
                 for part in ("Public", "Private")] if source.exists() else []
        for path in files:
            key = path.relative_to(root).as_posix()
            text = path.read_text(encoding="utf-8-sig")
            edges = set()
            for include in INCLUDE.findall(text):
                for base in [path.parent, *bases]:
                    candidate = base / include
                    if candidate.is_file():
                        edges.add(candidate.relative_to(root).as_posix())
                        break
                else:
                    # Missing project headers (deletions) still match a changed path by suffix.
                    edges.add(include)
            self.native[key] = edges
            names = REGISTRATION.findall(text)
            if names:
                self.registrations[key] = names

    def python_inputs(self, test):
        modules = closure(self.python, [f"Scripts/{test}"])
        return modules | {path for module in modules for path in self.reads.get(module, ())}

    def python_tests(self, changes):
        selected = {}
        for key in self.python:
            if not Path(key).name.startswith("Test"):
                continue
            inputs = self.python_inputs(Path(key).name)
            hits = [path for path in changes if any(path == p or path.startswith(p.rstrip("/") + "/")
                                                    for p in inputs)]
            if hits:
                selected[Path(key).name] = sorted(hits)
        return selected

    def rule_tests(self, changes):
        # A changed implementation reaches users of its interface, not every header used privately by that implementation.
        reverse = {}
        for key, edges in self.native.items():
            for edge in edges:
                reverse.setdefault(edge, set()).add(key)
        selected = {}
        for path in changes:
            seeds = [path, *(edge for edge in reverse if path.endswith("/" + edge))]
            if path.endswith(".cpp"):
                seeds.extend(key for key in self.native if key.endswith(".h") and Path(key).stem == Path(path).stem)
            for reached in closure(reverse, seeds):
                for name in self.registrations.get(reached, ()):
                    selected.setdefault(name, []).append(path)
        return {name: sorted(set(paths)) for name, paths in sorted(selected.items())}


def make_plan(root, changes, rules="None", filters=(), engine=None, base="origin/main"):
    changes = sorted({path.replace("\\", "/") for path in changes})
    if not filters and all(path.startswith(("docs/", ".github/")) or path.endswith(".md") for path in changes):
        return {"changes": changes, "steps": [], "suggested_rules": {}, "deferred": []}
    graph = Dependencies(root)
    python = graph.python_tests(changes)
    native_changes = [p for p in changes if p.startswith("Source/") or p.endswith(".uproject")]
    data_changes = [p for p in changes if p.startswith(DATA_ROOTS)]
    affected = graph.rule_tests(native_changes)
    steps, deferred = [], []
    python_command = [sys.executable, "-B"]
    pwsh = ["pwsh", "-NoProfile", "-File"]
    engine_args = ["-EngineRoot", engine] if engine else []
    common_inputs = ["Scripts/LocalVerification.py", "Scripts/Verify.ps1", "Scripts/CIChangeScope.py",
                     "Scripts/CheckRuleReport.py"]

    def add(key, kind, command, inputs, reason, outputs=()):
        steps.append({"id": key, "kind": kind, "command": command, "inputs": sorted(set(inputs + common_inputs)),
                      "reason": sorted(set(reason)), "outputs": list(outputs)})

    if data_changes:
        for name in ("SyncDemoWorldData", "SyncDemoContentData"):
            modules = closure(graph.python, [f"Scripts/{name}.py"])
            inputs = list(modules | {p for m in modules for p in graph.reads.get(m, ())})
            add(name, "sync", [*python_command, f"Scripts/{name}.py", "--check"], inputs, data_changes)
    if len(python) > MAX_PYTHON_MODULES:
        deferred.append(f"{len(python)} Python modules exceed local budget {MAX_PYTHON_MODULES}; use CI, not a local full suite.")
    else:
        for test, reasons in sorted(python.items()):
            inputs = list(graph.python_inputs(test)) + ["Scripts/RunPythonTests.py"]
            add(test, "python", [*python_command, "Scripts/RunPythonTests.py", "--pattern", test], inputs, reasons)
    for path in changes:
        if path.startswith("Scripts/") and path.endswith(".py") and not any(path in hits for hits in python.values()):
            deferred.append(f"No statically reachable Python test for {path}; CI covers dynamic dependencies.")
        if path.startswith(DATA_ROOTS) and not any(path in hits for hits in python.values()):
            deferred.append(f"No static Python reader for {path}; Sync checks and CI do not imply reader coverage.")

    plugin_inputs = [p.relative_to(root).as_posix() for p in (root / "Plugins").rglob("*")
                     if p.is_file() and p.suffix in (".h", ".cpp", ".cs", ".uplugin")
                     and not {"Binaries", "Intermediate"} & set(p.relative_to(root).parts)]
    native_inputs = ["Source", "Config", *plugin_inputs, "Lockdown.uproject", "Scripts/Common.ps1",
                     "Scripts/BuildEditor.ps1", "Scripts/Preflight.ps1", "SourceData", "Content"]
    if native_changes:
        add("editor", "build", [*pwsh, "Scripts/BuildEditor.ps1", *engine_args],
            native_inputs, native_changes, ["Binaries", *(["Plugins"] if (root / "Plugins").exists() else [])])
    chosen = sorted(set(filters) | (set(affected) if rules == "Auto" else set()))
    if chosen:
        # Validate explicit prefixes without ever allowing a local selection to become the complete suite.
        from CheckRuleReport import IMPORT_TESTS, REQUIRED_TESTS, select_tests
        selected = select_tests(chosen, skip_import=False)
        if len(selected) > MAX_RULE_TESTS or len(selected) == len(REQUIRED_TESTS):
            deferred.append(f"{len(selected)} rule tests exceed the local partial-run budget; use explicit RunTests.ps1 -Full or CI.")
        else:
            rule_scripts = ["Scripts", "Binaries"]
            add("rules", "rules", [*pwsh, "Scripts/RunTests.ps1", *engine_args, "-ImportTests",
                                  "Run" if selected & IMPORT_TESTS else "Skip",
                                  "-Base", base, "-Filter", ",".join(chosen)],
                native_inputs + rule_scripts, chosen)
    elif native_changes:
        deferred.append("Rule suggestions are informational; use -Rules Auto or -Filter to run a bounded partial selection.")
    if native_changes and not affected:
        deferred.append("No registered rule reached by the include graph; report this coverage gap and use CI.")
    unchecked = [p for p in changes if not (p.startswith(("docs/", ".github/", *DATA_ROOTS, "Source/"))
                                            or p.endswith((".md", ".uproject"))
                                            or (p.startswith("Scripts/") and p.endswith(".py")))]
    if unchecked:
        deferred.append("No automatic local check for: " + ", ".join(unchecked)
                        + "; choose the relevant check from VERIFICATION.md.")
    return {"changes": changes, "steps": steps, "suggested_rules": affected, "deferred": deferred}


def fingerprint(root, inputs):
    digest = hashlib.sha256()
    files = set()
    for name in sorted(set(inputs)):
        path = root / name
        digest.update((name + "\0" + ("directory" if path.is_dir() else "file") + "\0").encode())
        if path.is_dir():
            files.update(p for p in path.rglob("*") if p.is_file() and "__pycache__" not in p.parts)
        else:
            files.add(path)
    for path in sorted(files):
        digest.update(path.relative_to(root).as_posix().encode() + b"\0")
        if path.is_file():
            with path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
        else:
            digest.update(b"<missing>")
    return digest.hexdigest()


def environment_identity():
    return {"python": sys.version, "executable": sys.executable, "platform": sys.platform,
            "engine": os.environ.get("LD_UE_ROOT", ""),
            "toolchain": {key: os.environ.get(key, "") for key in ("PATH", "INCLUDE", "LIB")}}


def cache_key(root, step):
    identity = {"command": step["command"], "environment": environment_identity(),
                "inputs": fingerprint(root, step["inputs"])}
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()


def record_path(root, step):
    name = hashlib.sha256(step["id"].encode()).hexdigest()[:20]
    return root / "Artifacts" / "Verification" / f"{name}.json"


def cached_result(root, step):
    path = record_path(root, step)
    if not path.exists():
        return None
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
        log = (root / result["log"]).resolve()
        if not log.is_relative_to((root / "Artifacts/Verification").resolve()):
            raise ValueError("evidence outside Verification")
        if (result["key"] == cache_key(root, step) and log.is_file()
                and result["log_sha256"] == hashlib.sha256(log.read_bytes()).hexdigest()
                and result["outputs"] == fingerprint(root, step["outputs"])):
            return result
    except (ValueError, KeyError, TypeError) as error:
        print(f"WARNING: invalid verification record {path}: {error}")
    return None


def execute(root, command, log):
    with log.open("w", encoding="utf-8") as evidence:
        process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        try:
            for line in process.stdout:
                print(line, end="", flush=True)
                evidence.write(line)
            code = process.wait()
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()
        if code:
            raise RuntimeError(f"Check failed ({code}); evidence: {log}")


def run_step(root, step, force=False):
    previous = None if force else cached_result(root, step)
    if previous:
        print(f"SKIP {step['id']}: passed {previous['passed_at']}; Log: {previous['log']}")
        return previous
    path = record_path(root, step)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.unlink(missing_ok=True)
    key = cache_key(root, step)
    log = path.parent / f"{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}-{uuid.uuid4().hex[:12]}.log"
    print(f"RUN {step['id']}; Log: {log.relative_to(root)}", flush=True)
    execute(root, step["command"], log)
    if key != cache_key(root, step):
        raise RuntimeError(f"Inputs changed during {step['id']}; result not cached. Evidence: {log}")
    if any(not (root / name).exists() for name in step["outputs"]):
        raise RuntimeError(f"Missing output after {step['id']}; result not cached. Evidence: {log}")
    result = {"key": key, "passed_at": datetime.now(timezone.utc).isoformat(),
              "log": log.relative_to(root).as_posix(), "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
              "outputs": fingerprint(root, step["outputs"])}
    temp = path.with_suffix(f".{uuid.uuid4().hex}.tmp")
    temp.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    os.replace(temp, path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--path", action="append", help="simulate changed paths (plan only)")
    parser.add_argument("--rules", choices=("None", "Auto"), default="None")
    parser.add_argument("--filter", action="append", default=[])
    parser.add_argument("--engine")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if args.path and not args.plan:
        parser.error("--path is only allowed with --plan")
    paths = args.path if args.path else changed_paths(args.base, None)[0]
    filters = [item for value in args.filter for item in value.split(",") if item]
    plan = make_plan(ROOT, paths, args.rules, filters, args.engine, args.base)
    if args.json:
        if not args.plan:
            parser.error("--json requires --plan")
        print(json.dumps(plan, indent=2))
        return 0
    print(f"{len(paths)} changed files; dependency-based local plan (partial, not a CI gate).")
    for step in plan["steps"]:
        shown = ", ".join(step["reason"][:6])
        extra = f" (+{len(step['reason']) - 6} more; -Json lists all)" if len(step["reason"]) > 6 else ""
        print(f"  {step['id']}: {shown}{extra}")
        if args.plan:
            previous = None if args.force else cached_result(ROOT, step)
            if previous:
                print(f"    cached pass {previous['passed_at']}; Log: {previous['log']}")
    if plan["suggested_rules"]:
        print("Affected rule names (used as -Filter prefixes):")
        for name, reasons in list(plan["suggested_rules"].items())[:12]:
            print(f"  {name}: {', '.join(reasons)}")
        if len(plan["suggested_rules"]) > 12:
            print(f"  ... {len(plan['suggested_rules'])} total; use -Plan -Json for the full selection.")
    for reason in plan["deferred"]:
        print(f"DEFERRED: {reason}")
    if not args.plan:
        for step in plan["steps"]:
            run_step(ROOT, step, args.force)
        print("Selected local checks passed." if plan["steps"] else "Nothing to run locally for this change.")
        if plan["deferred"]:
            print("Coverage is incomplete; deferred checks above are NOT passes.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, SyntaxError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
