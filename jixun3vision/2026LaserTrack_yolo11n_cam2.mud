[basic]
type = axmodel
model_npu  = 2026LaserTrack_yolo11n_cam2_npu.axmodel
model_vnpu = 2026LaserTrack_yolo11n_cam2_vnpu.axmodel

[extra]
model_type = yolo11
type = detector
input_type = rgb
labels = target_paper
input_cache = true
output_cache = true
input_cache_flush = false
output_cache_inval = true
mean = 0,0,0
scale = 0.00392156862745098, 0.00392156862745098, 0.00392156862745098