import { useEffect, useRef, useState } from "react";
import "./StreamPlayer.css";

const RETRY_MS = 3000;
const POLL_MS = 120;
const STORAGE_KEY = "streamUrl";
const FRAME_MIME = "image/jpeg";
const JSON_FRAME_KEYS = ["image", "frame", "data", "jpg", "jpeg"];

function readSaved() {
  try {
    return localStorage.getItem(STORAGE_KEY) || "";
  } catch {
    return "";
  }
}

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

function isHttpUrl(url) {
  return /^https?:\/\//i.test(url);
}

function isWsUrl(url) {
  return /^wss?:\/\//i.test(url);
}

const STATUS_TEXT = {
  idle: "No stream",
  invalid: "Check the address",
  connecting: "Connecting",
  live: "Live",
  offline: "Offline, retrying",
  waiting: "Waiting for frames",
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

    let stopped = false;
    let retryTimer = null;
    let lastObjectUrl = null;
    let frames = 0;

    const show = (src, isObjectUrl) => {
      const img = imgRef.current;
      if (!img) return;
      img.src = src;
      if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
      lastObjectUrl = isObjectUrl ? src : null;
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

    // HTTP(S) JPEG poll (EC2 /api/v1/streams/<id>/jpeg)
    if (isHttpUrl(url)) {
      setStatus("connecting");
      let pollTimer = null;

      const tick = async () => {
        if (stopped) return;
        try {
          const res = await fetch(`${url}${url.includes("?") ? "&" : "?"}t=${Date.now()}`, {
            cache: "no-store",
          });
          if (res.status === 204) {
            setStatus((prev) => (prev === "live" ? "waiting" : prev === "connecting" ? "waiting" : prev));
          } else if (res.ok) {
            const blob = await res.blob();
            show(URL.createObjectURL(blob), true);
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
    }

    // Raw WebSocket JPEG/base64
    if (!isWsUrl(url)) {
      setStatus("invalid");
      clearInterval(fpsTimer);
      return;
    }

    setStatus((prev) => (prev === "offline" ? "offline" : "connecting"));

    let ws;
    try {
      ws = new WebSocket(url);
    } catch {
      retry();
      return () => {
        clearTimeout(retryTimer);
        clearInterval(fpsTimer);
      };
    }
    ws.binaryType = "blob";

    ws.onmessage = (event) => {
      if (typeof event.data === "string") {
        const src = textToSrc(event.data);
        if (src) show(src, false);
      } else {
        const blob = new Blob([event.data], { type: FRAME_MIME });
        show(URL.createObjectURL(blob), true);
      }
    };
    ws.onclose = retry;

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
      /* storage unavailable */
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
                <p>
                  Paste the EC2 JPEG poll URL, for example
                  {" "}http://18.223.70.95:8080/api/v1/streams/stream_1/jpeg
                </p>
              </>
            )}
            {status === "invalid" && (
              <>
                <h2>That address will not work</h2>
                <p>Use http(s):// for JPEG poll or ws(s):// for a raw WebSocket.</p>
              </>
            )}
            {status === "connecting" && <h2>Connecting to the stream</h2>}
            {status === "waiting" && (
              <>
                <h2>Connected, waiting for frames</h2>
                <p>Start the Pi thermal uplink to stream_1 or stream_2.</p>
              </>
            )}
            {status === "offline" && (
              <>
                <h2>Cannot reach the stream</h2>
                <p>
                  Retrying every {RETRY_MS / 1000} seconds. Check EC2 :8080 and the URL.
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
              placeholder="http://18.223.70.95:8080/api/v1/streams/stream_1/jpeg"
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
