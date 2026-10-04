import { useEffect, useRef, useState } from "react";
import { apiUrl } from "../resourceIds";
import "./LidarPanel.css";

const POLL_MS = 500;
const RANGE_MM = 2000;
const DEFAULT_SIZE = 480;

function paintScan(canvas, scan) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const size = canvas.width;
  const img = ctx.createImageData(size, size);
  const data = img.data;

  for (let i = 0; i < size * size; i += 1) {
    const o = i * 4;
    data[o] = 12;
    data[o + 1] = 12;
    data[o + 2] = 16;
    data[o + 3] = 255;
  }

  const put = (x, y, r, g, b) => {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    const o = (y * size + x) * 4;
    data[o] = r;
    data[o + 1] = g;
    data[o + 2] = b;
    data[o + 3] = 255;
  };

  const bins = scan?.distance_mm;
  const intens = scan?.intensity;
  const binCount = Number(scan?.bin_count) || (bins?.length ?? 0);
  if (!Array.isArray(bins) || binCount <= 0) {
    ctx.putImageData(img, 0, 0);
    return;
  }

  const cx = (size - 1) * 0.5;
  const cy = (size - 1) * 0.5;
  const radius = Math.min(cx, cy) * 0.92;

  for (let bin = 0; bin < binCount; bin += 1) {
    const hitMm = Number(bins[bin]) || 0;
    if (hitMm <= 0) continue;
    const intensVal = Number(intens?.[bin] ?? 128);
    const deg = bin * 0.5;
    const rad = (deg * Math.PI) / 180;
    const rn = Math.min(Math.max(hitMm / RANGE_MM, 0), 1);
    const x = Math.round(cx + radius * rn * Math.sin(rad));
    const y = Math.round(cy + radius * rn * Math.cos(rad));

    const t = 1 - rn;
    const bright = 0.55 + 0.45 * (intensVal / 255);
    let rf = 0;
    let gf = 0;
    let bf = 0;
    if (t < 0.33) {
      const u = t / 0.33;
      gf = 40 + 80 * u;
      bf = 255;
    } else if (t < 0.66) {
      const u = (t - 0.33) / 0.33;
      rf = 255 * u;
      gf = 120 + 135 * u;
      bf = 255 * (1 - u);
    } else {
      const u = (t - 0.66) / 0.34;
      rf = 255;
      gf = 255 * (1 - u);
    }
    const r = Math.min(255, Math.max(0, Math.round(rf * bright)));
    const g = Math.min(255, Math.max(0, Math.round(gf * bright)));
    const b = Math.min(255, Math.max(0, Math.round(bf * bright)));
    put(x, y, r, g, b);
    put(x + 1, y, r, g, b);
    put(x, y + 1, r, g, b);
  }

  const ox = Math.round(cx);
  const oy = Math.round(cy);
  for (let d = -2; d <= 2; d += 1) {
    put(ox + d, oy, 240, 240, 245);
    put(ox, oy + d, 240, 240, 245);
  }

  ctx.putImageData(img, 0, 0);
}

export default function LidarPanel({ lidarId, size = DEFAULT_SIZE }) {
  const canvasRef = useRef(null);
  const [status, setStatus] = useState("connecting");
  const [meta, setMeta] = useState({ bins: 0, speed: 0 });
  const px = Number(size) || DEFAULT_SIZE;

  useEffect(() => {
    if (!lidarId) return undefined;
    let stopped = false;
    let timer = null;

    const tick = async () => {
      try {
        const res = await fetch(apiUrl(`/api/v1/lidar/${lidarId}`), {
          cache: "no-store",
        });
        if (stopped) return;
        if (!res.ok) {
          setStatus("offline");
        } else {
          const scan = await res.json();
          if (stopped) return;
          if (scan?.available && Array.isArray(scan.distance_mm)) {
            setStatus("live");
            setMeta({
              bins: Number(scan.valid_bins) || 0,
              speed: Number(scan.speed_dps) || 0,
            });
            const canvas = canvasRef.current;
            if (canvas) paintScan(canvas, scan);
          } else {
            setStatus("waiting");
          }
        }
      } catch {
        if (!stopped) setStatus("offline");
      }
      if (!stopped) timer = setTimeout(tick, POLL_MS);
    };

    tick();
    return () => {
      stopped = true;
      clearTimeout(timer);
    };
  }, [lidarId]);

  const label =
    status === "live"
      ? `Live ${meta.bins} bins`
      : status === "waiting"
        ? "Waiting for scan"
        : status === "offline"
          ? "Offline"
          : "Connecting";

  return (
    <section
      className="lidar"
      aria-label="Lidar scan"
      style={{ width: px, flex: "0 0 auto" }}
    >
      <div
        className="lidar__frame"
        style={{
          width: px,
          height: px,
          minWidth: px,
          minHeight: px,
          maxWidth: px,
          maxHeight: px,
          boxSizing: "border-box",
          position: "relative",
          overflow: "hidden",
        }}
      >
        <canvas
          ref={canvasRef}
          className="lidar__canvas"
          width={px}
          height={px}
          style={{
            position: "absolute",
            top: 0,
            left: 0,
            width: px,
            height: px,
            display: "block",
          }}
        />
        {status !== "live" && (
          <div className="lidar__notice">
            <h2>{label}</h2>
            <p>Polling {lidarId} every {POLL_MS / 1000}s.</p>
          </div>
        )}
        <div className={`lidar__status lidar__status--${status}`} role="status">
          <span className="lidar__dot" aria-hidden="true" />
          {label}
        </div>
      </div>
      <p className="lidar__caption">Lidar</p>
    </section>
  );
}
