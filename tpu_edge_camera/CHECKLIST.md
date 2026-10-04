# tpu_edge_camera

IR + thermal predator fusion uplink to FireSight EC2. GPS on UART4 (`/dev/ttyAMA4`); lidar later.

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
      GpsDriver.*               # UART4 NMEA -> HUD overlay + /ingest/gps
      LidarDriver.*             # unused for now
    vision/
      CameraFusion.*         # thermal / IR / predator fusion + JPEG
      LidarMap.* DisplayHud.*
    net/
      UplinkClient.* Auth0M2M.*
```

## Win path

1. Deploy updated `ec2_cloud_backend` (raw JPEG store + `POST /ingest/frame/jpeg`)
2. On Pi: `sudo apt install libcamera-dev`, confirm `rpicam-hello` sees Arducam IR
3. Build `tpu_edge_camera`, set `STREAM_ID` / `EC2_BASE_URL` / `TARGET_FPS=20`
4. Touchscreen HUD: direct `/dev/fb0` blit (`HUD_ENABLED=true`, optional `HUD_FB_DEVICE`). Must run on a console VT (not under the desktop compositor). User needs `video` group. Quit with `q` on stdin.
5. Frontend: `VITE_STREAM_URL=/api/v1/streams/stream_1/mjpeg` (push; do not use `/jpeg` poll)

## Latency notes

- Thermal `GetFrameData` blocks ~16 Hz; reader runs on a background thread so IR/fusion is not gated.
- Uplink posts raw JPEG over HTTP keep-alive (`/api/v1/ingest/frame/jpeg`); JSON base64 is fallback only.
- Website must use `/mjpeg` push. Snapshot `/jpeg` poll is capped by RTT (~4 fps on ~250 ms links).

## HUD from SSH

Framebuffer blit cannot win against a desktop compositor. `sudo ./compile_and_run`
stops LightDM / graphical.target automatically when HUD is enabled:

```bash
sudo ./compile_and_run
```

Expect log lines `HUD: releasing DSI panel`, `DisplayHud claimed VT2`, `KD_GRAPHICS`,
and RGB test bars on the panel at startup. If bars never appear, set
`HUD_KEEP_DESKTOP=0` (default) and confirm no compositor still holds DRM.
For uplink-only: `HUD_ENABLED=false`.

## Sensor notes

- IR: CSI Arducam / OV5647 via libcamera (`HAS_LIBCAMERA`). Prefer camera id containing `ov5647`.
- Thermal: I2C1 `/dev/i2c-1` @ `0x33` (MLX90640), async reader.
- Fusion: port of `C_Hardware_Testing` FusionScreen (EMA, ambient percentile, alpha 0.225, x-shift -2.5).
- Missing hardware falls back to demo buffers so uplink/HUD still run.
- GPS: `/dev/ttyAMA4` @ 9600, `$GPRMC`/`$GNRMC`. Logs `GpsDriver live` on open; posts ~1 Hz when fix valid. User needs `dialout`.

## EC2 targets

- Health: `GET http://100.103.164.9:8080/health`
- Ingest: `POST /api/v1/ingest/frame` `{stream_id, frame}`
- Poll: `GET /api/v1/streams/stream_1/jpeg`
