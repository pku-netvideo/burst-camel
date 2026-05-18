# 仿真说明

`test/network_simulator.[ch]` 提供合成网络仿真能力，用于端到端 replay/simulation 测试：

- 可配置：capacity、base delay、loss rate、router buffer（bytes）
- 输出：frame event（recv/lost/queue delay 等统计）

## 预留 trace 接口

`camel_network_simulator_load_trace_file()` 预留从真实 trace 文件加载场景的入口，方便后续接入真实 trace。

