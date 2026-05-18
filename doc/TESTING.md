# 测试说明

## 运行测试

在项目根目录：

```bash
make -C test clean
make -C test
./test/fcc_unittest
```

## 与原项目一致性

迁移过程中，单测优先直接移植原项目 `video_cc_testbed/framecc_test` 中对应的测试序列与断言语义，用于确保行为一致。

