import { useEffect, useState } from "react";
import "./StreamList.css";

const PROBE_TIMEOUT_MS = 5000;

const STATE_TEXT = {
  checking: "Checking",
  online: "Online",
  offline: "Offline",
};

function isWsUrl(url) {
  return /^wss?:\/\//i.test(url || "");
}

function isHttpUrl(url) {
  return /^https?:\/\//i.test(url || "") || (url || "").startsWith("/");
}

// Briefly probes each stream (WebSocket open, or HTTP HEAD/GET).
function useAvailability(streams) {
  const [states, setStates] = useState({});

  useEffect(() => {
    if (!streams) return;
    setStates({});
    const cleanups = streams.map((stream, i) => {
      let done = false;
      let ws = null;
      let abort = null;
      const finish = (state) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        setStates((prev) => ({ ...prev, [i]: state }));
        if (ws) {
          ws.onopen = ws.onerror = ws.onclose = null;
          ws.close();
        }
        if (abort) abort.abort();
      };
      const timer = setTimeout(() => finish("offline"), PROBE_TIMEOUT_MS);

      if (isWsUrl(stream.url)) {
        try {
          ws = new WebSocket(stream.url);
          ws.onopen = () => finish("online");
          ws.onerror = () => finish("offline");
          ws.onclose = () => finish("offline");
        } catch {
          finish("offline");
        }
      } else if (isHttpUrl(stream.url)) {
        abort = new AbortController();
        fetch(stream.url, {
          method: "GET",
          cache: "no-store",
          signal: abort.signal,
          headers: { Accept: "multipart/x-mixed-replace, image/jpeg, */*" },
        })
          .then((res) => {
            // 200 with body, or 204 (no frame yet) both mean the endpoint is up.
            finish(res.ok || res.status === 204 ? "online" : "offline");
          })
          .catch(() => finish("offline"));
      } else {
        finish("offline");
      }

      return () => {
        done = true;
        clearTimeout(timer);
        if (ws) {
          ws.onopen = ws.onerror = ws.onclose = null;
          ws.close();
        }
        if (abort) abort.abort();
      };
    });
    return () => cleanups.forEach((stop) => stop());
  }, [streams]);

  return states;
}

export default function StreamList({ streams, loadError, onRefresh }) {
  const states = useAvailability(streams);

  if (!streams) {
    return <p className="streams__message">Loading the stream list</p>;
  }

  const onlineCount = streams.filter((_, i) => states[i] === "online").length;

  return (
    <section className="streams">
      <div className="streams__toolbar">
        <p className="streams__summary">
          {loadError
            ? "Can't load the stream list."
            : streams.length === 0
            ? "No streams listed yet."
            : `${onlineCount} of ${streams.length} online`}
        </p>
        <button type="button" className="btn" onClick={onRefresh}>
          Refresh
        </button>
      </div>

      {loadError && (
        <p className="streams__message">
          Check that streams.json exists in the project's public folder and is
          valid JSON, then rebuild.
        </p>
      )}

      {!loadError && streams.length === 0 && (
        <p className="streams__message">
          Add a name and stream URL (MJPEG …/mjpeg or WebSocket) to
          streams.json, then rebuild.
        </p>
      )}

      <ul className="streams__list">
        {streams.map((stream, i) => {
          const state = states[i] || "checking";
          return (
            <li key={`${stream.url}-${i}`}>
              <a className="streams__item" href={`#/stream/${i}`}>
                <span className="streams__text">
                  <span className="streams__name">{stream.name}</span>
                  <span className="streams__url">{stream.url}</span>
                </span>
                <span className={`streams__state streams__state--${state}`}>
                  <span className="streams__dot" aria-hidden="true" />
                  {STATE_TEXT[state]}
                </span>
              </a>
            </li>
          );
        })}
      </ul>
    </section>
  );
}
