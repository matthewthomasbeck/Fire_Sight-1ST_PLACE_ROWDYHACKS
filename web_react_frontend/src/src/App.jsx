import { useState } from "react";
import StreamPlayer from "./components/StreamPlayer";
import LoginPage from "./components/LoginPage";
import { getSession, signOut } from "./auth";

// Vite reads this from .env. On Create React App, use
// process.env.REACT_APP_STREAM_URL instead.
const DEFAULT_STREAM_URL = import.meta.env.VITE_STREAM_URL || "";

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
