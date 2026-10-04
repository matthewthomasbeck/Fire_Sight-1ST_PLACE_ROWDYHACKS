import { useEffect, useState } from "react";
import { streamKind } from "../streamKind";
import "./StreamList.css";

const PROBE_TIMEOUT_MS = 5000;

const STATE_TEXT = {
  checking: "Checking",
  online: "Online",
  offline: "Offline",
};

// Briefly connects to one stream and reports "online" or "offline".
// Returns a function that cancels the check.
function probe(url, report) {
  const kind = streamKind(url);
  let done = false;
  let stop = () => {};

  const finish = (state) => {
    if (done) return;
    done = true;
    clearTimeout(timer);
    stop();
    if (state) report(state);
  };
  const timer = setTimeout(() => finish("offline"), PROBE_TIMEOUT_MS);

  if (kind === "ws") {
    try {
      const ws = new WebSocket(url);
      stop = () => {
        ws.onopen = ws.onerror = ws.onclose = null;
        ws.close();
      };
      ws.onopen = () => finish("online");
      ws.onerror = () => finish("offline");
      ws.onclose = () => finish("offline");
    } catch {
      finish("offline");
    }
  } else if (kind === "mjpeg") {
    const img = new Image();
    stop = () => {
      img.onload = img.onerror = null;
      img.src = ""; // drops the connection
    };
    img.onload = () => finish("online"); // first frame arrived
    img.onerror = () => finish("offline");
    img.src = url;
  } else {
    finish("offline");
  }

  return () => finish(null);
}

function useAvailability(streams) {
  const [states, setStates] = useState({});

  useEffect(() => {
    if (!streams) return;
    setStates({});
    const cancels = streams.map((stream, i) =>
      probe(stream.url, (state) =>
        setStates((prev) => ({ ...prev, [i]: state }))
      )
    );
    return () => cancels.forEach((cancel) => cancel());
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
          Add a name and address for each stream to public/streams.json, then
          rebuild.
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
