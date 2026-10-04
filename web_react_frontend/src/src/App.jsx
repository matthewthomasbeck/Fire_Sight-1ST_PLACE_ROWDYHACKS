import { useCallback, useEffect, useState } from "react";
import StreamPlayer from "./components/StreamPlayer";
import StreamList from "./components/StreamList";
import LoginPage from "./components/LoginPage";
import { getSession, signOut } from "./auth";

// The list of streams. By default this is public/streams.json, which you can
// edit without touching the code. Point VITE_STREAMS_URL at a server endpoint
// instead if you want the list to be generated live.
const STREAMS_URL =
  import.meta.env.VITE_STREAMS_URL || `${import.meta.env.BASE_URL}streams.json`;

async function fetchStreams() {
  const res = await fetch(STREAMS_URL, { cache: "no-store" });
  if (!res.ok) throw new Error("missing");
  const data = await res.json();
  const list = Array.isArray(data) ? data : data.streams;
  if (!Array.isArray(list)) throw new Error("format");
  return list
    .filter((s) => s && typeof s.url === "string")
    .map((s) => ({ name: s.name || s.url, url: s.url }));
}

// Tiny hash router: "#/" is the list, "#/stream/2" is the third stream.
function useHash() {
  const [hash, setHash] = useState(window.location.hash);
  useEffect(() => {
    const onChange = () => setHash(window.location.hash);
    window.addEventListener("hashchange", onChange);
    return () => window.removeEventListener("hashchange", onChange);
  }, []);
  return hash;
}

export default function App() {
  const [session, setSession] = useState(getSession);
  const [streams, setStreams] = useState(null); // null = still loading
  const [loadError, setLoadError] = useState(false);
  const [reloadKey, setReloadKey] = useState(0);
  const hash = useHash();

  useEffect(() => {
    if (!session) return;
    let cancelled = false;
    setLoadError(false);
    fetchStreams()
      .then((list) => !cancelled && setStreams(list))
      .catch(() => {
        if (cancelled) return;
        setStreams([]);
        setLoadError(true);
      });
    return () => {
      cancelled = true;
    };
  }, [session, reloadKey]);

  const refresh = useCallback(() => setReloadKey((k) => k + 1), []);

  if (!session) {
    return (
      <LoginPage
        onSignedIn={(s) => {
          window.location.hash = "#/"; // always land on the stream list
          setSession(s);
        }}
      />
    );
  }

  const handleSignOut = () => {
    signOut();
    setStreams(null);
    setSession(null);
  };

  const account = (
    <div className="page__account">
      <span>{session.username}</span>
      <button type="button" className="btn" onClick={handleSignOut}>
        Sign out
      </button>
    </div>
  );

  const match = hash.match(/^#\/stream\/(\d+)$/);

  // ---------- one stream ----------
  if (match) {
    const stream = streams ? streams[Number(match[1])] : null;
    return (
      <main className="page">
        <a className="page__back" href="#/">
          ← All streams
        </a>
        <header className="page__header">
          <h1 className="page__title">
            {stream ? stream.name : streams ? "Stream not found" : "Loading"}
          </h1>
          {account}
        </header>
        {stream && <StreamPlayer url={stream.url} />}
        {streams && !stream && (
          <p>That stream isn't in the list any more. Go back and pick another.</p>
        )}
      </main>
    );
  }

  // ---------- list of streams ----------
  return (
    <main className="page">
      <header className="page__header">
        <h1 className="page__title">Streams</h1>
        {account}
      </header>
      <StreamList streams={streams} loadError={loadError} onRefresh={refresh} />
    </main>
  );
}
