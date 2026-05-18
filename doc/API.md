# API 参考

本项目对外 API 通过 [camel.h](file:///home/pic/documents/burst-camel/src/include/camel.h) 聚合导出。

## 带宽估计器

- `camel_estimator_init`
- `camel_estimator_add_sample`
- `camel_estimator_get_bandwidth`
- `camel_estimator_get_bdp_bytes`

## 拥塞检测器

- `camel_congestion_detector_init`
- `camel_congestion_detector_add_sample`

## Burst 控制器

- `camel_burst_controller_init`
- `camel_burst_controller_record_interval(_counts)`
- `camel_burst_controller_maybe_update`

## Receiver

- `camel_receiver_create`
- `camel_receiver_on_received_frame_info`

## Sender

- `camel_sender_create`
- `camel_sender_send_frame`
- `camel_sender_on_feedback`
- `camel_sender_heartbeat`

## Feedback 编解码

- `camel_feedback_msg_encode`
- `camel_feedback_msg_decode`

