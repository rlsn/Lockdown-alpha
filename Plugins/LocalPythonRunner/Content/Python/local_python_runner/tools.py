"""Unreal Toolset Registry adapter. No scene edits occur on import."""
import json
from pathlib import Path

import unreal
from toolset_registry import tool_call
from .core import Runner


def _runner():
    plugin_root = Path(__file__).resolve().parents[3]
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())
    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    return Runner(plugin_root / "Config" / "runner.json", project, saved)


def _json(value):
    return json.dumps(value, ensure_ascii=False)


@unreal.uclass()
class LocalPythonFileTools(unreal.ToolsetDefinition):
    """在当前 UE 中运行已检查的本地 Python 文件，无需操作屏幕。"""

    @tool_call
    @staticmethod
    def get_runner_status() -> str:
        """只读查询当前工程、进程及允许执行的目录。

        Returns:
            JSON 状态信息。执行前必须核对工程。
        """
        return _json(_runner().status())

    @tool_call
    @staticmethod
    def inspect_python_file(script_path: str) -> str:
        """检查文件与语法并取得 SHA-256；不执行脚本。

        Args:
            script_path: 现有 .py 文件的绝对路径。
        Returns:
            包含 sha256 和 project_file 的 JSON 检查结果。
        """
        return _json(_runner().inspect(script_path))

    @tool_call
    @staticmethod
    def run_python_file(script_path: str, expected_sha256: str,
                        request_id: str, expected_project: str) -> str:
        """在编辑器线程执行用户授权的脚本，保留 __main__ 和 __file__ 语义。

        必须先调用 inspect_python_file。超时后查询原 request_id，禁止自动换 ID 重跑。
        同一个编辑器只安排一个任务执行；脚本可能修改场景，失败不等于已回滚。
        不要把另一个正在进行的任务的脚本当成测试脚本。

        Args:
            script_path: 允许目录内的 Python 文件绝对路径。
            expected_sha256: 本次检查返回的 SHA-256；只校验入口文件，不包括依赖。
            request_id: 为本次执行生成的唯一 ID；重试使用相同 ID。
            expected_project: 状态工具确认过的 .uproject 绝对路径。
        Returns:
            执行状态、Python 输出、异常与磁盘记录路径，均为 JSON。
        """
        return _json(_runner().run(script_path, expected_sha256, request_id, expected_project))

    @tool_call
    @staticmethod
    def get_script_request(request_id: str) -> str:
        """查询既有执行记录，不重新运行脚本。

        Args:
            request_id: 执行时使用的 ID。
        Returns:
            JSON 记录。running_or_interrupted 表示结果未确认，不能盲目重跑。
        """
        return _json(_runner().get_request(request_id))


def register():
    if not unreal.ToolsetRegistry.is_available():
        raise RuntimeError("Toolset Registry 尚未可用。")
    if not unreal.ToolsetRegistry.is_toolset_class_registered(LocalPythonFileTools):
        unreal.ToolsetRegistry.register_toolset_class(LocalPythonFileTools)
    unreal.log("[LocalPythonRunner] 本地脚本工具已注册；未执行任何用户脚本。")
