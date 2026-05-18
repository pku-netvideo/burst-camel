# 算法说明

## 帧级带宽估计（Bandwidth Estimator）

- 带宽样本：
  - 对于一个 frame，取第 2..n 个包的字节数之和（排除首包），除以该 frame 的接收时间跨度得到带宽样本。
- 平滑：
  - 使用 EWMA 维护 `avg_bandwidth_bps`。
- BDP：
  - 维护 `min_delay_us`（近似 RTprop）。
  - `BDP_bytes = avg_bandwidth_bps * min_delay_us / 8 / 1e6`。

对应实现：
- [bandwidth_estimator.h](file:///home/pic/documents/burst-camel/src/include/bandwidth_estimator.h)
- [bandwidth_estimator.c](file:///home/pic/documents/burst-camel/src/bandwidth_estimator.c)

## 拥塞检测（Congestion Detector）

- 输入：`inflight_bytes` 与 `delay_us` 的样本序列（窗口）。
- 计算：对 `delay` vs `inflight` 做线性回归，得到斜率 `dD / dinflight`。
- 判定：
  - `slope > threshold` 认为拥塞，`gamma *= 0.95`，并 clamp 到 `min_gamma`。
  - 否则 `gamma = 1.0`。

对应实现：
- [congestion_detector.h](file:///home/pic/documents/burst-camel/src/include/congestion_detector.h)
- [congestion_detector.c](file:///home/pic/documents/burst-camel/src/congestion_detector.c)

## 突发长度控制（Burst Controller）

- 以 2KB 为 interval，统计 interval 级别的 `sent/lost`。
- 基准丢包率：interval 0 的丢包率 `L0`。
- excess loss：
  - 如果任一 interval 的 `Li > L0 + 0.1`，则 burst 以 2KB 下降；到达最小 burst 则进入 fallback。
  - 否则 burst 以 2KB 上升并 clamp 到最大 burst。

对应实现：
- [burst_controller.h](file:///home/pic/documents/burst-camel/src/include/burst_controller.h)
- [burst_controller.c](file:///home/pic/documents/burst-camel/src/burst_controller.c)

