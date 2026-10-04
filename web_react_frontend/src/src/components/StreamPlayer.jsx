import { useEffect, useRef, useState } from "react";
import "./StreamPlayer.css";

const RETRY_MS = 3000;
const FRAME_MIME = "image/jpeg";
const JSON_FRAME_KEYS = ["frame", "image", "jpeg", "data"];

function isWsUrl(url) {
  return /^wss?:\/\//i.test(url || "");
}

function isHttpUrl(url) {
  return /^https?:\/\//i.test(url || "") || (url || "").startsWith("/");
}

function toMjpegUrl(url) {
  if (!url) return "";
  if (url.includes("/mjpeg")) return url;
  if (url.includes("/jpeg")) return url.replace(/\/jpeg(\b|$)/, "/mjpeg$1");
  return url;
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

const STATUS_TEXT = {
  invalid: "Check the address",
  connecting: "Connecting",
  live: "Live",
  offline: "Offline, retrying",
  waiting: "Waiting for frames",
};

// Shows one stream from the list. Supports MJPEG http(s) and WebSocket urls.
export default function StreamPlayer({ url }) {
  const [status, setStatus] = useState("connecting");
  const [attempt, setAttempt] = useState(0);
  const [fps, setFps] = useState(0);
  const [modeLabel, setModeLabel] = useState("");

  const imgRef = useRef(null);
  const frameRef = useRef(null);

  useEffect(() => {
    const mjpegUrl = isHttpUrl(url) ? toMjpegUrl(url) : "";
    const useWs = isWsUrl(url);
    const useMjpeg = !useWs && isHttpUrl(mjpegUrl);

    if (!useWs && !useMjpeg) {
      setStatus("invalid");
      setModeLabel("");
      return;
    }

    setStatus((prev) => (prev === "offline" ? "offline" : "connecting"));
    setModeLabel(useWs ? "WS" : "MJPEG");

    let stopped = false;
    let retryTimer = null;
    let waitingTimer = null;
    let fpsTimer = null;
    let lastObjectUrl = null;
    let frames = 0;
    let ws = null;

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

    fpsTimer = setInterval(() => {
      setFps(frames);
      frames = 0;
    }, 1000);

    if (useWs) {
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
          const blob = new Blob([event.data], { type: FRAME_MIME });
          show(URL.createObjectURL(blob), true);
        }
      };
      ws.onclose = retry;
    } else {
      const img = imgRef.current;
      if (!img) return;

      const onLoad = () => {
        if (stopped) return;
        setStatus("live");
        frames += 1;
      };
      const onError = () => {
        if (!stopped) retry();
      };

      img.addEventListener("load", onLoad);
      img.addEventListener("error", onError);
      img.src = `${mjpegUrl}${mjpegUrl.includes("?") ? "&" : "?"}t=${Date.now()}`;

      waitingTimer = setTimeout(() => {
        if (!stopped) {
          setStatus((prev) => (prev === "connecting" ? "waiting" : prev));
        }
      }, 2500);

      return () => {
        stopped = true;
        clearTimeout(retryTimer);
        clearTimeout(waitingTimer);
        clearInterval(fpsTimer);
        img.removeEventListener("load", onLoad);
        img.removeEventListener("error", onError);
        img.src = "";
        if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
      };
    }

    return () => {
      stopped = true;
      clearTimeout(retryTimer);
      clearTimeout(waitingTimer);
      clearInterval(fpsTimer);
      if (ws) {
        ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
        ws.close();
      }
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
                  Use an MJPEG http(s) URL (…/mjpeg) or a WebSocket address
                  (ws:// / wss://) in streams.json.
                </p>
              </>
            )}
            {status === "connecting" && <h2>Connecting to the stream</h2>}
            {status === "waiting" && (
              <>
                <h2>Connected, waiting for frames</h2>
                <p>Start the Pi uplink. Redeploy EC2 if /mjpeg 404s.</p>
              </>
            )}
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
          {status === "live" && (
            <span aria-hidden="true">
              {modeLabel} {fps} fps
            </span>
          )}
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
