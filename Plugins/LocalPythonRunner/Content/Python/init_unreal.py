"""Register tools only. Never run user scripts or restart MCP on startup."""
import unreal

try:
    from local_python_runner.tools import register
    register()
except Exception as error:
    unreal.log_error("[LocalPythonRunner] 工具注册失败: " + str(error))
