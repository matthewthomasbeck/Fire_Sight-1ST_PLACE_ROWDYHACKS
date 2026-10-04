import { useState } from "react";
import StreamPlayer from "./components/StreamPlayer";
import LoginPage from "./components/LoginPage";
import { getSession, signOut } from "./auth";

// Vite reads these from .env. Prefer full JPEG poll URL; fall back to API base.
const API_BASE = (import.meta.env.VITE_API_BASE_URL || "").replace(/\/$/, "");
const DEFAULT_STREAM_URL =
  import.meta.env.VITE_STREAM_URL ||
  (API_BASE ? `${API_BASE}/api/v1/streams/stream_1/jpeg` : "");

export default function App() {
  const [session, setSession] = useState(getSession);

  if (!session) {
    return <LoginPage onSignedIn={setSession} />;
  }

  const handleSignOut = () => {
    signOut();
    setSession(null);
  };

  return (
    <main className="page">
      <header className="page__header">
        <h1 className="page__title">Live stream</h1>
        <div className="page__account">
          <span>{session.username}</span>
          <button type="button" className="btn" onClick={handleSignOut}>
            Sign out
          </button>
        </div>
      </header>
      <StreamPlayer defaultUrl={DEFAULT_STREAM_URL} />
    </main>
  );
}
