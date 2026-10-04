import { useEffect, useRef, useState } from "react";
import "./StreamPlayer.css";

const RETRY_MS = 3000;
// If your server sends PNG or WebP frames as raw binary/base64, change this.
const FRAME_MIME = "image/jpeg";
// Field names checked when the server sends JSON messages.
const JSON_FRAME_KEYS = ["image", "frame", "data", "jpg", "jpeg"];

// Turns one text message into something an <img> can show, or null.
function textToSrc(text) {
  let value = text.trim();
  if (value.startsWith("{")) {
    try {
      const msg = JSON.parse(value);
      const key = JSON_FRAME_KEYS.find((k) => typeof msg[k] === "string");
      if (!key) return null;
      value = msg[key];
    } catch {
      return null;
    }
  }
  if (!value) return null;
  return value.startsWith("data:") ? value : `data:${FRAME_MIME};base64,${value}`;
}

const STATUS_TEXT = {
  invalid: "Check the address",
  connecting: "Connecting",
  live: "Live",
  offline: "Offline, retrying",
};

// Shows one WebSocket stream. The address comes from the stream list.
export default function StreamPlayer({ url }) {
  const [status, setStatus] = useState("connecting");
  const [attempt, setAttempt] = useState(0);
  const [fps, setFps] = useState(0);

  const imgRef = useRef(null);
  const frameRef = useRef(null);

  useEffect(() => {
    if (!/^wss?:\/\//i.test(url || "")) {
      setStatus("invalid");
      return;
    }
    setStatus((prev) => (prev === "offline" ? "offline" : "connecting"));

    let stopped = false;
    let retryTimer = null;
    let lastObjectUrl = null;
    let frames = 0;

    const retry = () => {
      if (stopped) return;
      setStatus("offline");
      setFps(0);
      retryTimer = setTimeout(() => setAttempt((a) => a + 1), RETRY_MS);
    };

    const show = (src, isObjectUrl) => {
      const img = imgRef.current;
      if (!img) return;
      img.src = src;
      if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
      lastObjectUrl = isObjectUrl ? src : null;
      frames += 1;
      setStatus("live");
    };

    let ws;
    try {
      ws = new WebSocket(url);
    } catch {
      retry();
      return () => clearTimeout(retryTimer);
    }
    ws.binaryType = "blob";

    ws.onmessage = (event) => {
      if (typeof event.data === "string") {
        const src = textToSrc(event.data);
        if (src) show(src, false);
      } else {
        // Binary message: one encoded image per message.
        const blob = new Blob([event.data], { type: FRAME_MIME });
        show(URL.createObjectURL(blob), true);
      }
    };
    ws.onclose = retry; // also fires after a connection error

    const fpsTimer = setInterval(() => {
      setFps(frames);
      frames = 0;
    }, 1000);

    return () => {
      stopped = true;
      clearTimeout(retryTimer);
      clearInterval(fpsTimer);
      ws.close();
      if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
    };
  }, [url, attempt]);

  const goFullscreen = () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else frameRef.current?.requestFullscreen?.();
  };

  return (
    <section className="stream">
      <div className="stream__frame" ref={frameRef}>
        <img ref={imgRef} className="stream__media" alt="Live stream" />

        {status !== "live" && (
          <div className="stream__notice">
            {status === "invalid" && (
              <>
                <h2>This stream's address won't work</h2>
                <p>
                  A WebSocket address starts with ws:// or wss://. Fix it in
                  streams.json.
                </p>
              </>
            )}
            {status === "connecting" && <h2>Connecting to the stream</h2>}
            {status === "offline" && (
              <>
                <h2>Can't reach the stream</h2>
                <p>
                  Trying again every {RETRY_MS / 1000} seconds. Check that the
                  stream server is running.
                </p>
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

      <div className="stream__controls">
        <div className="stream__actions">
          <button type="button" className="btn" onClick={goFullscreen}>
            Full screen
          </button>
        </div>
      </div>
    </section>
  );
}
