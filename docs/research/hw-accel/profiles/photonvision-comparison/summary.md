## AprilTag on the Lenovo camera, 1280x720@30 (the scene held about 16-22 tags)
system | decimation | result rate fps | latency ms (mean / p95) | process cores | RSS MB | GPU % | tags per frame
PhotonVision v2026.3.4 (CPU, 4 threads) | 1 | 29.7 | 38.0 / 40.0 | 2.29 | 280 | 0 | 19.2
PhotonVision v2026.3.4 (CPU, 4 threads) | 2 | 29.7 | 27.1 / 30.7 | 1.24 | 282 | 0 | 16.3
LumenVision Vulkan, shipped defaults | 2 | 30.0 | 15.6 / 15.9 | 0.21 | 246 | 3 | 16.0
LumenVision Vulkan, shipped defaults | 1 | 30.0 | 18.4 / 19.4 | 0.31 | 360 | 10 | 22.3
LumenVision Vulkan, ultrafast refine + margin 4 | 2 | 30.0 | 14.6 / 15.3 | 0.15 | 246 | 3 | 16.1
LumenVision Vulkan, ultrafast refine + margin 4 | 1 | 30.0 | 17.7 / 18.0 | 0.21 | 360 | 9 | 21.2
LumenVision CPU (libapriltag), 4 threads | 2 | 30.0 | 19.0 / 20.1 | 0.56 | 181 | 0 | 15.1
LumenVision CPU (libapriltag), 4 threads | 1 | 30.0 | 31.2 / 35.2 | 1.67 | 195 | 0 | 22.5

## Object detection, YOLOv8n COCO (the same .rknn), Lenovo camera 1280x720@30
system | result rate fps | latency ms | process cores | RSS MB | NPU % | detections per frame
PhotonVision | 29.8 | 39.2 | 0.74 | 238 | 15.1 | 1.31
LumenVision | 30.0 | 28.8 | 0.08 | 170 | 13.3 | 1.24
LumenVision stages (mean ms): process 16.12, rknn-rga 1.42, rknn-run 13.57, rknn-decode+nms 0.62

## AprilTag on the stereo camera, whole 2560x720@60 frame
system | decimation | result rate fps | latency ms (mean / p95) | process cores | RSS MB | GPU %
PhotonVision (CPU, 4 threads) | 1 | 30.0 | 38.1 / 41.6 | 1.73 | 338 | 0
PhotonVision (CPU, 4 threads) | 2 | 30.2 | 33.6 / 36.7 | 1.23 | 339 | 0
LumenVision Vulkan, shipped defaults | 2 | 60.1 | 19.8 / 23.2 | 0.15 | 295 | 4
LumenVision Vulkan, shipped defaults | 1 | 60.0 | 20.0 / 23.5 | 0.15 | 520 | 4
LumenVision CPU (libapriltag), 4 threads | 2 | 60.0 | 21.3 / 24.8 | 0.42 | 191 | 0
LumenVision CPU (libapriltag), 4 threads | 1 | 60.0 | 24.1 / 27.6 | 0.94 | 203 | 0
