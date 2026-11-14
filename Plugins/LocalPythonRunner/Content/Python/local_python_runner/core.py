"""File runner with project checks and persistent, non-repeating request IDs.

This is an explicitly installed trusted-script capability, not a Python sandbox.
Only the entry file is hashed; imported dependencies remain trusted local code.
"""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import sys
import threading
import time
import traceback
import types

_EXECUTION_LOCK = threading.Lock()
_REQUEST_PATTERN = re.compile(r"[A-Za-z0-9_-]{8,100}\Z")


def canonical(path):
    return Path(path).resolve(strict=True)


def path_key(path):
    return os.path.normcase(str(canonical(path)))


class _Capture(io.TextIOBase):
    def __init__(self, original, limit):
        self.original, self.limit = original, limit
        self.parts, self.count, self.truncated = [], 0, False

    def write(self, text):
        text = str(text)
        remaining = max(0, self.limit - self.count)
        self.parts.append(text[:remaining]) if remaining else None
        self.count += min(len(text), remaining)
        self.truncated |= len(text) > remaining
        if self.original is not None:
            self.original.write(text)
        return len(text)

    def flush(self):
        if self.original is not None:
            self.original.flush()

    def value(self):
        return "".join(self.parts)


class Runner:
    def __init__(self, config_path, project_file, saved_dir):
        self.config_path = Path(config_path)
        self.project_file = canonical(project_file)
        self.saved_dir = Path(saved_dir).resolve()

    def config(self):
        config = json.loads(self.config_path.read_text(encoding="utf-8-sig"))
        if not config.get("expected_project") or not config.get("allowed_roots"):
            raise ValueError("执行器尚未配置；请使用 ue_runner.py install-plugin 绑定工程和脚本目录。")
        # Repository defaults are portable; an installation may still supply absolute local bindings.
        def resolve_binding(value):
            path = Path(value)
            return str(canonical(path if path.is_absolute() else self.config_path.parent / path))
        config["expected_project"] = resolve_binding(config["expected_project"])
        config["allowed_roots"] = [resolve_binding(root) for root in config["allowed_roots"]]
        if path_key(config["expected_project"]) != path_key(self.project_file):
            raise ValueError("当前工程与执行器配置不匹配；拒绝运行脚本。")
        return config

    def status(self):
        config = self.config()
        return {"ready": True, "project_file": str(self.project_file),
                "pid": os.getpid(), "allowed_roots": config["allowed_roots"],
                "records_directory": str(self.saved_dir / "LocalPythonRunner" / "requests"),
                "runs_on_editor_thread": True,
                "entry_hash_does_not_cover_imported_dependencies": True}

    def _read_script(self, script_path):
        config = self.config()
        if not Path(script_path).is_absolute():
            raise ValueError("脚本必须使用绝对路径。")
        path = canonical(script_path)
        if path.suffix.lower() != ".py" or not path.is_file():
            raise ValueError("只接受现有的 .py 文件。")
        roots = [canonical(root) for root in config["allowed_roots"]]
        if not any(path.is_relative_to(root) for root in roots):
            raise ValueError("脚本不在配置允许的目录内。")
        limit = int(config.get("max_script_bytes", 2097152))
        with path.open("rb") as source_file:
            source = source_file.read(limit + 1)
        if len(source) > limit:
            raise ValueError("脚本超过配置的大小限制。")
        # Compile the exact bytes that were hashed, avoiding a second file read.
        code = compile(source, str(path), "exec", dont_inherit=True)
        return path, source, code, config

    def inspect(self, script_path):
        path, source, _, _ = self._read_script(script_path)
        return {"ready": True, "executed": False, "script_path": str(path),
                "sha256": hashlib.sha256(source).hexdigest(), "size_bytes": len(source),
                "project_file": str(self.project_file), "syntax_valid": True}

    def _record_path(self, request_id):
        if not _REQUEST_PATTERN.fullmatch(request_id):
            raise ValueError("request_id 必须是 8–100 位字母、数字、下划线或连字符。")
        return self.saved_dir / "LocalPythonRunner" / "requests" / (request_id + ".json")

    def get_request(self, request_id):
        self.config()
        path = self._record_path(request_id)
        if not path.exists():
            return {"state": "not_found", "request_id": request_id}
        result = json.loads(path.read_text(encoding="utf-8"))
        result["record_path"] = str(path)
        result["executed_this_call"] = False
        return result

    def run(self, script_path, expected_sha256, request_id, expected_project):
        if threading.current_thread() is not threading.main_thread():
            raise RuntimeError("必须从 UE 编辑器主线程调用。")
        self.config()
        if path_key(expected_project) != path_key(self.project_file):
            raise ValueError("调用方指定的工程与当前工程不符。")
        if not re.fullmatch(r"[a-fA-F0-9]{64}", expected_sha256):
            raise ValueError("必须提供 inspect_python_file 返回的 SHA-256。")
        expected_sha256 = expected_sha256.lower()
        record_path = self._record_path(request_id)
        # This catches recursive calls as well as other callers of this runner.
        if not _EXECUTION_LOCK.acquire(blocking=False):
            raise RuntimeError("已有脚本正在执行，请等待其结果，不要重复提交。")
        try:
            if record_path.exists():
                previous = self.get_request(request_id)
                same_request = (previous["script_path"] == str(Path(script_path).resolve())
                                and previous["sha256"] == expected_sha256
                                and previous["project_file"] == str(self.project_file))
                if not same_request:
                    raise ValueError("request_id 已用于不同的脚本或版本。")
                previous["replayed"] = True
                previous["executed_this_call"] = False
                return previous
            path, source, code, config = self._read_script(script_path)
            actual_hash = hashlib.sha256(source).hexdigest()
            if actual_hash != expected_sha256:
                raise ValueError("检查后脚本内容已改变；请重新检查。")
            record = {"state": "running_or_interrupted", "ok": False,
                      "request_id": request_id, "script_path": str(path),
                      "sha256": actual_hash, "project_file": str(self.project_file),
                      "pid": os.getpid(), "started_at_unix": time.time(),
                      "replayed": False, "executed_this_call": True,
                      "record_path": str(record_path)}
            record_path.parent.mkdir(parents=True, exist_ok=True)
            # Reserve BEFORE execution. A crash leaves a record that prevents replay.
            with record_path.open("x", encoding="utf-8") as output:
                json.dump(record, output, ensure_ascii=False, indent=2)
                output.flush()
                os.fsync(output.fileno())
            limit = int(config.get("max_output_chars", 32000))
            stdout, stderr = _Capture(sys.stdout, limit), _Capture(sys.stderr, limit)
            old_main = sys.modules.get("__main__")
            old_argv, old_path = sys.argv, sys.path[:]
            old_bytecode, old_cwd = sys.dont_write_bytecode, os.getcwd()
            main = types.ModuleType("__main__")
            main.__dict__.update(__file__=str(path), __package__=None,
                                 __spec__=None, __cached__=None)
            try:
                sys.modules["__main__"] = main
                sys.argv = [str(path)]
                sys.path.insert(0, str(path.parent))
                sys.dont_write_bytecode = True
                with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                    exec(code, main.__dict__)
                record.update(state="succeeded", ok=True)
            except BaseException as error:
                # Do not let SystemExit from a user script terminate the editor.
                record.update(state="failed", ok=False,
                              error_type=type(error).__name__, error=str(error),
                              traceback=traceback.format_exc()[-limit:])
            finally:
                if old_main is None:
                    sys.modules.pop("__main__", None)
                else:
                    sys.modules["__main__"] = old_main
                sys.argv, sys.path[:] = old_argv, old_path
                sys.dont_write_bytecode = old_bytecode
                if os.getcwd() != old_cwd:
                    os.chdir(old_cwd)
            record.update(stdout=stdout.value(), stderr=stderr.value(),
                          output_truncated=stdout.truncated or stderr.truncated,
                          finished_at_unix=time.time())
            temporary = record_path.with_suffix(".tmp")
            temporary.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
            os.replace(temporary, record_path)
            return record
        finally:
            _EXECUTION_LOCK.release()
