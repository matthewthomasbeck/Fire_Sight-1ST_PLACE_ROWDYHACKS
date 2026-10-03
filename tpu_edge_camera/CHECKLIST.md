# tpu_edge_camera

Thermal-first uplink to FireSight EC2. IR, lidar, and GPS come later.

## Layout

```text
tpu_edge_camera/
  CMakeLists.txt
  compile_and_run
  config/edge.env.example
  src/
    main.cpp                 # thermal loop only
    config/Config.*
    util/Log.* Base64.* stb_image_write.h
    hardware/
      ThermalDriver.*        # live (MLX90640) + demo fallback
      mlx90640/              # vendored Melexis/Pimoroni
      CameraDriver.* LidarDriver.* GpsDriver.*  # unused for now
    vision/
      CameraFusion.*         # thermal false-color + JPEG
      LidarMap.* DisplayHud.*
    net/
      UplinkClient.* Auth0M2M.*
```

## Thermal win path

1. Deploy updated `ec2_cloud_backend` (ResourceStore + `/streams/<id>/jpeg`)
2. On Pi: `sudo apt install libopencv-dev`, build `tpu_edge_camera`, set `STREAM_ID` / `EC2_BASE_URL`
3. Touchscreen HUD: `HUD_ENABLED=true` (default panel 800x480 via `HUD_WIDTH` / `HUD_HEIGHT`). From SSH: `DISPLAY=:0 ./compile_and_run`. Quit with `q` or Esc.
4. Frontend: `VITE_STREAM_URL=http://18.223.70.95:8080/api/v1/streams/stream_1/jpeg`

## EC2 targets

- Health: `GET http://100.103.164.9:8080/health`
- Ingest: `POST /api/v1/ingest/frame` `{stream_id, frame}`
- Poll: `GET /api/v1/streams/stream_1/jpeg`
