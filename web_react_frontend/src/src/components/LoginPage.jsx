import { useState } from "react";
import { signIn } from "../auth";
import "./LoginPage.css";

export default function LoginPage({ onSignedIn }) {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [showPassword, setShowPassword] = useState(false);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const submit = async (e) => {
    e.preventDefault();
    if (busy) return;
    setError("");
    setBusy(true);
    try {
      const session = await signIn(username.trim(), password);
      onSignedIn(session);
    } catch (err) {
      setError(err.message);
      setBusy(false);
    }
  };

  return (
    <main className="login">
      <form className="login__form" onSubmit={submit} noValidate>
        <h1 className="login__title">Sign in to watch the stream</h1>

        <div className="login__field">
          <label htmlFor="login-username">Username</label>
          <input
            id="login-username"
            type="text"
            autoComplete="username"
            autoCapitalize="none"
            spellCheck="false"
            autoFocus
            required
            value={username}
            onChange={(e) => setUsername(e.target.value)}
          />
        </div>

        <div className="login__field">
          <label htmlFor="login-password">Password</label>
          <div className="login__password">
            <input
              id="login-password"
              type={showPassword ? "text" : "password"}
              autoComplete="current-password"
              required
              value={password}
              onChange={(e) => setPassword(e.target.value)}
            />
            <button
              type="button"
              className="login__toggle"
              onClick={() => setShowPassword((s) => !s)}
              aria-pressed={showPassword}
            >
              {showPassword ? "Hide" : "Show"}
            </button>
          </div>
        </div>

        {error && (
          <p className="login__error" role="alert">
            {error}
          </p>
        )}

        <button
          type="submit"
          className="btn btn--primary login__submit"
          disabled={busy || !username.trim() || !password}
        >
          {busy ? "Signing in" : "Sign in"}
        </button>
      </form>
    </main>
  );
}
