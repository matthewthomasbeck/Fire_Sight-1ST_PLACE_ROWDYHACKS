# tpu_edge_camera

IR + thermal predator fusion uplink to FireSight EC2. Lidar and GPS come later.

## Layout

```text
tpu_edge_camera/
  CMakeLists.txt             # optional libcamera via pkg-config
  compile_and_run
  config/edge.env.example
  src/
    main.cpp                 # IR + thermal fusion loop
    config/Config.*
    util/Log.* Base64.* stb_image_write.h
    hardware/
      CameraDriver.*         # live (libcamera OV5647) + demo fallback
      ThermalDriver.*        # live (MLX90640) + demo fallback
      mlx90640/              # vendored Melexis/Pimoroni
      LidarDriver.* GpsDriver.*  # unused for now
    vision/
      CameraFusion.*         # thermal / IR / predator fusion + JPEG
      LidarMap.* DisplayHud.*
    net/
      UplinkClient.* Auth0M2M.*
```

## Win path

1. Deploy updated `ec2_cloud_backend` (ResourceStore + `/streams/<id>/jpeg`)
2. On Pi: `sudo apt install libcamera-dev`, confirm `rpicam-hello` sees Arducam IR
3. Build `tpu_edge_camera`, set `STREAM_ID` / `EC2_BASE_URL`
4. Touchscreen HUD: direct `/dev/fb0` blit (`HUD_ENABLED=true`, optional `HUD_FB_DEVICE`). No OpenCV/X11. Prefer console / no desktop compositor owning the panel. User needs `video` group. Quit with `q` on stdin.
5. Frontend: `VITE_STREAM_URL=http://18.223.70.95:8080/api/v1/streams/stream_1/jpeg`

## Sensor notes

- IR: CSI Arducam / OV5647 via libcamera (`HAS_LIBCAMERA`). Prefer camera id containing `ov5647`.
- Thermal: I2C1 `/dev/i2c-1` @ `0x33` (MLX90640).
- Fusion: port of `C_Hardware_Testing` FusionScreen (EMA, ambient percentile, alpha 0.225, x-shift -2.5).
- Missing hardware falls back to demo buffers so uplink/HUD still run.

## EC2 targets

- Health: `GET http://100.103.164.9:8080/health`
- Ingest: `POST /api/v1/ingest/frame` `{stream_id, frame}`
- Poll: `GET /api/v1/streams/stream_1/jpeg`
