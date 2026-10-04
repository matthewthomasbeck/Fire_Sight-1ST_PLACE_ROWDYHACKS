import { useEffect, useRef, useState } from "react";
import "./StreamPlayer.css";

const RETRY_MS = 3000;
const POLL_MS = 120;

function isHttpUrl(url) {
  // Absolute http(s), or same-origin path (e.g. /api/v1/streams/stream_1/jpeg)
  return /^https?:\/\//i.test(url) || url.startsWith("/");
}

function pollUrl(url) {
  const sep = url.includes("?") ? "&" : "?";
  return `${url}${sep}t=${Date.now()}`;
}

const STATUS_TEXT = {
  idle: "No stream",
  invalid: "Bad stream URL",
  connecting: "Connecting",
  live: "Live",
  offline: "Offline, retrying",
  waiting: "Waiting for frames",
};

export default function StreamPlayer({ streamUrl = "" }) {
  const [status, setStatus] = useState(streamUrl ? "connecting" : "idle");
  const [attempt, setAttempt] = useState(0);
  const [fps, setFps] = useState(0);

  const imgRef = useRef(null);
  const frameRef = useRef(null);

  useEffect(() => {
    if (!streamUrl) {
      setStatus("idle");
      return;
    }
    if (!isHttpUrl(streamUrl)) {
      setStatus("invalid");
      return;
    }

    let stopped = false;
    let retryTimer = null;
    let pollTimer = null;
    let lastObjectUrl = null;
    let frames = 0;

    const show = (src) => {
      const img = imgRef.current;
      if (!img) return;
      img.src = src;
      if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
      lastObjectUrl = src;
      frames += 1;
      setStatus("live");
    };

    const retry = () => {
      if (stopped) return;
      setStatus("offline");
      setFps(0);
      retryTimer = setTimeout(() => setAttempt((a) => a + 1), RETRY_MS);
    };

    const fpsTimer = setInterval(() => {
      setFps(frames);
      frames = 0;
    }, 1000);

    setStatus("connecting");

    const tick = async () => {
      if (stopped) return;
      try {
        const res = await fetch(pollUrl(streamUrl), { cache: "no-store" });
        if (res.status === 204) {
          setStatus((prev) =>
            prev === "live" || prev === "connecting" || prev === "waiting" ? "waiting" : prev
          );
        } else if (res.ok) {
          const blob = await res.blob();
          show(URL.createObjectURL(blob));
        } else {
          retry();
          return;
        }
      } catch {
        retry();
        return;
      }
      pollTimer = setTimeout(tick, POLL_MS);
    };

    tick();

    return () => {
      stopped = true;
      clearTimeout(retryTimer);
      clearTimeout(pollTimer);
      clearInterval(fpsTimer);
      if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
    };
  }, [streamUrl, attempt]);

  const goFullscreen = () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else frameRef.current?.requestFullscreen?.();
  };

  return (
    <section className="stream">
      <div
        className="stream__frame"
        ref={frameRef}
        onDoubleClick={goFullscreen}
        title="Double-click for full screen"
      >
        <img
          ref={imgRef}
          className="stream__media"
          alt="Live thermal stream"
        />

        {status !== "live" && (
          <div className="stream__notice">
            {status === "idle" && (
              <>
                <h2>Stream not configured</h2>
                <p>Set VITE_STREAM_URL to the EC2 JPEG poll endpoint and rebuild.</p>
              </>
            )}
            {status === "invalid" && (
              <>
                <h2>Stream URL is invalid</h2>
                <p>VITE_STREAM_URL must be an http(s) JPEG poll URL or a same-origin /api path.</p>
              </>
            )}
            {status === "connecting" && <h2>Connecting to the stream</h2>}
            {status === "waiting" && (
              <>
                <h2>Connected, waiting for frames</h2>
                <p>Start the Pi thermal uplink to stream_1.</p>
              </>
            )}
            {status === "offline" && (
              <>
                <h2>Cannot reach the stream</h2>
                <p>Retrying every {RETRY_MS / 1000} seconds. Check EC2 on port 8080.</p>
              </>
            )}
          </div>
        )}

        <div className={`stream__status stream__status--${status}`} role="status">
          <span className="stream__dot" aria-hidden="true" />
          {STATUS_TEXT[status]}
          {status === "live" && <span aria-hidden="true">{fps} fps</span>}
        </div>
      </div>
    </section>
  );
}
