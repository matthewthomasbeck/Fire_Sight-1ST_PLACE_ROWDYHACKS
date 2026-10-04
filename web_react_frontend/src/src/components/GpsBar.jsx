import { useEffect, useState } from "react";
import { apiUrl } from "../resourceIds";
import "./GpsBar.css";

const POLL_MS = 1000;

function fmt(n, digits = 6) {
  if (typeof n !== "number" || Number.isNaN(n)) return "--";
  return n.toFixed(digits);
}

export default function GpsBar({ gpsId }) {
  const [status, setStatus] = useState("connecting");
  const [fix, setFix] = useState(null);

  useEffect(() => {
    if (!gpsId) return undefined;
    let stopped = false;
    let timer = null;

    const tick = async () => {
      try {
        const res = await fetch(apiUrl(`/api/v1/gps/${gpsId}`), {
          cache: "no-store",
        });
        if (stopped) return;
        if (!res.ok) {
          setStatus("offline");
          setFix(null);
        } else {
          const body = await res.json();
          if (stopped) return;
          if (
            body?.available
            && typeof body.lat === "number"
            && typeof body.lon === "number"
          ) {
            setStatus("live");
            setFix({ lat: body.lat, lon: body.lon });
          } else {
            setStatus("waiting");
            setFix(null);
          }
        }
      } catch {
        if (!stopped) {
          setStatus("offline");
          setFix(null);
        }
      }
      if (!stopped) timer = setTimeout(tick, POLL_MS);
    };

    tick();
    return () => {
      stopped = true;
      clearTimeout(timer);
    };
  }, [gpsId]);

  return (
    <section className={`gps gps--${status}`} aria-label="GPS coordinates">
      <div className="gps__label">
        <span className="gps__dot" aria-hidden="true" />
        GPS
      </div>
      {fix ? (
        <p className="gps__coords">
          <span>
            <span className="gps__key">Lat</span> {fmt(fix.lat)}
          </span>
          <span>
            <span className="gps__key">Lon</span> {fmt(fix.lon)}
          </span>
        </p>
      ) : (
        <p className="gps__coords gps__coords--empty">
          {status === "offline"
            ? "Can't reach GPS"
            : status === "waiting"
              ? "Awaiting satellite lock"
              : "Connecting"}
        </p>
      )}
      <span className="gps__id">{gpsId}</span>
    </section>
  );
}
