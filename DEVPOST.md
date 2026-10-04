# FireSight

**Meet you through the fire.**

FireSight is a wearable firefighter monitoring system. A Raspberry Pi fuses infrared and thermal video on-device, overlays 360° lidar and GPS, and pushes live JPEG + telemetry to a Flask hub on AWS EC2. Dispatchers watch Firefighter 1 / Firefighter 2 streams at [firesight.casa](https://firesight.casa): predator-style heat vision, a lidar map, and GPS, all in one dashboard.

---

## Inspiration

We both know fire and its devastation up close. Matthew Martinez fought fires in the U.S. Navy for 5 years and saw the worst it can do. Matthew Beck recently watched a fire take his father's home. Those experiences were years apart, but the gap looked the same: crews still lean on willpower and wit while the gear stays behind. We wanted a firefighter body cam that actually helps in the field, fuses heat with vision, and does not break the bank.

---

## What it does

1. **Capture** IR (Arducam OV5647) and thermal (MLX90640) on a Raspberry Pi 4, plus LD19 lidar and UART GPS.
2. **Fuse on the edge** into a predator-style RGB frame (thermal tint over IR), then JPEG-encode at ~20 FPS target.
3. **Uplink** raw JPEG, GPS fixes, and lidar scans over HTTP keep-alive to EC2 (`POST /api/v1/ingest/...`).
4. **Serve** live MJPEG per firefighter stream, plus JSON GPS and lidar for the SPA.
5. **Watch** at firesight.casa: pick Firefighter 1 or 2, see fused video, lidar panel, and GPS bar.

Optional on-helmet path: a 3.5" DSI framebuffer HUD shows the same fusion + lidar inset + GPS without X11.

---

## How we built it: three systems working together

This is not one monolith. It is **three systems** that only meet at HTTP.

### 1. Edge firmware (`tpu_edge_camera`, C++17)

Runs on a Raspberry Pi 4. One process owns IR (libcamera), an async MLX90640 reader (so ~16 Hz I2C thermal does not gate IR), LD19 UART lidar, and NMEA GPS on UART4. `CameraFusion` builds the predator overlay (registration shift, EMA ambient, mount rotation). A worker thread posts the latest JPEG with keep-alive so uplink latency does not stall the HUD. Demo drivers keep the pipeline alive when a sensor is missing.

### 2. Cloud backend (Flask on AWS EC2)

The hub in the middle. It stores the latest frame / GPS / lidar per resource id (`stream_1`/`stream_2`, `gps_*`, `lidar_*`), fans frames out as multipart MJPEG (`boundary=firesightframe`), and validates Auth0 user JWTs (JWKS) on control-plane routes like `/me` and stream/GPS/lidar lists. Ingest is push-from-Pi, not cloud pull; background workers and Socket.IO video are scaffolded but the live path is HTTP ingest + MJPEG.

### 3. Front end (React + Vite + nginx)

The dashboard at firesight.casa. nginx serves the SPA and reverse-proxies `/api/` to Flask on :8080. The watch view uses MJPEG for video, polls lidar ~500 ms into a polar canvas, and polls GPS ~1 s. The SPA never talks to the Pi; it only talks to the backend.

### How they hand off

1. Pi captures IR + thermal, fuses, JPEG-encodes, and POSTs to `/api/v1/ingest/frame/jpeg?stream_id=stream_N`.
2. GPS and lidar post on their own cadence to `/ingest/gps` and `/ingest/lidar`.
3. Flask updates the in-memory store and advances the MJPEG sequence.
4. A signed-in dispatcher opens Firefighter 1 or 2; the browser loads `/api/v1/streams/stream_N/mjpeg` and polls paired `gps_N` / `lidar_N`.
5. nginx keeps the site same-origin so the SPA stays thin and CORS stays simple.

Each layer can restart on its own. Firmware owns sensors and fusion, EC2 owns the latest truth for each firefighter, React owns viewing.

---

## How we followed the tracks

### Auth0 track
- **User JWTs on the API:** Flask validates Auth0 JWKS tokens (audience `https://rowdyhacks.dispatch.api`) on identity and list endpoints so the control plane is tied to a real issuer, not an open admin API.
- **Edge M2M scaffolding:** Pi env and C++ client are wired for audience `https://rowdyhacks.edge.api` so device uplink can move to machine-to-machine tokens without redesigning the ingest contract.
- Dual audiences separate dispatcher traffic from future device/service traffic.

### UI/UX track
- Dispatch UI is built around stress, not dashboards: stream pick, then one watch composition (video + lidar + GPS).
- On-device HUD mirrors what command sees, so the firefighter and the dispatcher share the same fused picture.
- Resource naming is human first ("Firefighter 1 / 2") while IDs stay `stream_N` under the hood.

---

## Challenges we ran into

- **Thermal vs IR timing:** MLX90640 `GetFrameData` blocks around 16 Hz; a background thermal thread keeps fusion IR-paced near 20 FPS.
- **Uplink without killing the HUD:** separate JPEG worker + keep-alive raw POST (base64 JSON only as fallback) so HTTP RTT does not freeze the framebuffer.
- **Pi display ownership:** claiming `/dev/fb0` for the DSI HUD meant fighting the desktop/compositor so DRM stayed available.
- **Sensor registration:** thermal x-shift, mirror, and 90° mount correction so the predator overlay lines up on a 240x320 frame.
- **Cloud media without a media server:** in-memory dual-stream MJPEG fanout under hackathon time, not WebRTC or a full VMS.
- **Auth across layers:** JWKS on Flask is real; closing the loop on SPA Auth0 SDK and edge M2M tokens is the remaining wire, not a greenfield design.

---

## Accomplishments we're proud of

- End-to-end loop: physical IR + thermal + lidar + GPS → C++ fusion → EC2 → live site on a real domain.
- On-device predator fusion with async thermal, not a cloud-only heat map.
- Binary JPEG ingest and MJPEG push instead of slow poll-and-base64 video.
- Dual-firefighter resource model with matched video, GPS, and lidar.
- Firmware in C++17 on constrained hardware, with demo fallbacks so the demo still runs if a cable fails.
- Custom 3D-printed mounts for the wearable stack.

---

## What we learned

- Embedded constraints force a tiny, explicit contract between device and server (JPEG bytes + small JSON telemetry).
- Keeping firmware, backend, and front end separate made each layer debuggable without taking the whole stack down.
- Push-from-edge + MJPEG beats pull workers and WebSocket scaffolding when the demo clock is ticking.
- Auth that only lives in the UI is not auth; validating JWTs on the API is the part that ships.
- Fireground UX is about one clear picture shared by the person in the smoke and the person behind the screen.

---

## What's next

- Finish Auth0 on the SPA and M2M on Pi ingest so every frame path is identity-gated.
- Raise sustained FPS and stress-test uplink under weak Wi-Fi.
- Harden MJPEG/WebSocket health so dispatch sees online/offline without guessing.
- Optional Google Maps view using the existing authenticated `/maps/config` hook.
- Revisit NVIDIA / edge prediction once the sensor loop is rock solid.

---

## Built with

- Raspberry Pi 4
- C++17 / CMake / libcamera
- MLX90640 (thermal)
- Arducam OV5647 (IR)
- LD19 lidar
- UART GPS
- Flask
- React / Vite
- nginx / Docker
- AWS EC2
- Auth0 (JWKS user JWT; M2M scaffolding)
- Tailscale (private path Pi ↔ EC2)
- 3D printing / modeling

---

## Try it

1. Open [firesight.casa](https://firesight.casa).
2. Sign in when prompted.
3. Wait for Firefighter 1 / Firefighter 2 to show live.
4. Open a stream: fused IR+thermal video, lidar panel, GPS bar.

*Live feeds depend on edge devices being powered and uplinking.*
