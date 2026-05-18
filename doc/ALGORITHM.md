# Algorithm Notes

## Bandwidth Estimator

- Bandwidth sample:
  - For one packet group, take the sum of bytes from packet 2..n (exclude the first packet), divided by the receive time span, to obtain a bandwidth sample.
- Smoothing:
  - Maintain `avg_bandwidth_bps` using EWMA.
- BDP:
  - Maintain `min_delay_us` (approx. RTprop).
  - `BDP_bytes = avg_bandwidth_bps * min_delay_us / 8 / 1e6`.

Implementation:
- [bandwidth_estimator.h](file:///home/pic/documents/burst-camel/src/include/bandwidth_estimator.h)
- [bandwidth_estimator.c](file:///home/pic/documents/burst-camel/src/bandwidth_estimator.c)

## Congestion Detector

- Input: a sliding window of `(inflight_bytes, delay_us)` samples.
- Computation: linear regression over `delay` vs `inflight`, producing slope `dD / dinflight`.
- Decision:
  - If `slope > threshold`, treat it as congestion, apply `gamma *= 0.95`, and clamp to `min_gamma`.
  - Otherwise set `gamma = 1.0`.

Implementation:
- [congestion_detector.h](file:///home/pic/documents/burst-camel/src/include/congestion_detector.h)
- [congestion_detector.c](file:///home/pic/documents/burst-camel/src/congestion_detector.c)

## Burst Controller

- Use 2KB intervals and track per-interval `sent/lost`.
- Baseline loss: interval 0 loss rate `L0`.
- Excess loss:
  - If any interval has `Li > L0 + 0.1`, decrease burst by 2KB; if burst hits minimum, enter fallback.
  - Otherwise increase burst by 2KB and clamp to the maximum.

Implementation:
- [burst_controller.h](file:///home/pic/documents/burst-camel/src/include/burst_controller.h)
- [burst_controller.c](file:///home/pic/documents/burst-camel/src/burst_controller.c)
