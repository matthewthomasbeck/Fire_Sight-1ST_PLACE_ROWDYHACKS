import { useEffect, useRef, useState } from "react";
import "./StreamPlayer.css";

const RETRY_MS = 3000;
const STORAGE_KEY = "streamUrl";
// If your server sends PNG or WebP frames as raw binary/base64, change this.
const FRAME_MIME = "image/jpeg";
// Field names checked when the server sends JSON messages.
const JSON_FRAME_KEYS = ["image", "frame", "data", "jpg", "jpeg"];

function readSaved() {
  try {
    return localStorage.getItem(STORAGE_KEY) || "";
  } catch {
    return "";
  }
}

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
  idle: "No stream",
  invalid: "Check the address",
  connecting: "Connecting",
  live: "Live",
  offline: "Offline, retrying",
};

export default function StreamPlayer({ defaultUrl = "" }) {
  const [url, setUrl] = useState(() => readSaved() || defaultUrl);
  const [draft, setDraft] = useState(url);
  const [status, setStatus] = useState(url ? "connecting" : "idle");
  const [attempt, setAttempt] = useState(0);
  const [fps, setFps] = useState(0);

  const imgRef = useRef(null);
  const frameRef = useRef(null);

  useEffect(() => {
    if (!url) {
      setStatus("idle");
      return;
    }
    if (!/^wss?:\/\//i.test(url)) {
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

  const connect = (e) => {
    e.preventDefault();
    const next = draft.trim();
    try {
      localStorage.setItem(STORAGE_KEY, next);
    } catch {
      /* storage unavailable, keep going */
    }
    setStatus(next ? "connecting" : "idle");
    setUrl(next);
    setAttempt((a) => a + 1);
  };

  const goFullscreen = () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else frameRef.current?.requestFullscreen?.();
  };

  return (
    <section className="stream">
      <div className="stream__frame" ref={frameRef}>
        <img
          ref={imgRef}
          className="stream__media"
          alt="Live stream from the EC2 instance"
        />

        {status !== "live" && (
          <div className="stream__notice">
            {status === "idle" && (
              <>
                <h2>No stream address yet</h2>
                <p>Paste the WebSocket address of your EC2 stream below and connect.</p>
              </>
            )}
            {status === "invalid" && (
              <>
                <h2>That address won't work</h2>
                <p>A WebSocket address starts with ws:// or wss://.</p>
              </>
            )}
            {status === "connecting" && <h2>Connecting to the stream</h2>}
            {status === "offline" && (
              <>
                <h2>Can't reach the stream</h2>
                <p>
                  Trying again every {RETRY_MS / 1000} seconds. Check that the
                  instance is running and its WebSocket port is open in the
                  security group.
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
        <form className="stream__source" onSubmit={connect}>
          <label htmlFor="stream-url">Stream address</label>
          <div className="stream__row">
            <input
              id="stream-url"
              type="text"
              inputMode="url"
              spellCheck="false"
              autoComplete="off"
              placeholder="ws://your-ec2-address:8765"
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
            />
            <button type="submit" className="btn btn--primary">
              Connect
            </button>
          </div>
        </form>

        <div className="stream__actions">
          <button type="button" className="btn" onClick={goFullscreen} disabled={!url}>
            Full screen
          </button>
        </div>
      </div>
    </section>
  );
}
