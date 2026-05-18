# 实现说明

## 代码组织

- `src/include/`：对外头文件
- `src/`：模块实现
- `test/`：单元测试与仿真测试

## 目标

独立化以原项目 `/home/pic/documents/video_cc_testbed` 的实现为真值来源，迁移过程中尽量保持算法与行为一致，仅做必要的依赖替换与符号重命名。

