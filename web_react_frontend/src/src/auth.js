// Sign-in logic lives here so the page doesn't care how it works.
//
// Two modes, chosen by .env:
//   1. VITE_LOGIN_URL set  -> POSTs { username, password } as JSON to your server.
//      Expects 200 on success (optionally with { token }), 401/403 on bad credentials.
//   2. Otherwise           -> demo mode, compares against VITE_DEMO_USER / VITE_DEMO_PASSWORD.
//      Demo credentials ship inside the built site, so anyone can read them.
//      Fine for a demo, not real protection.

const LOGIN_URL = import.meta.env.VITE_LOGIN_URL || "";
const DEMO_USER = import.meta.env.VITE_DEMO_USER || "";
const DEMO_PASSWORD = import.meta.env.VITE_DEMO_PASSWORD || "";
const SESSION_KEY = "session";

export function getSession() {
  try {
    return JSON.parse(sessionStorage.getItem(SESSION_KEY)) || null;
  } catch {
    return null;
  }
}

function saveSession(session) {
  try {
    sessionStorage.setItem(SESSION_KEY, JSON.stringify(session));
  } catch {
    /* storage unavailable: the session lasts until the page reloads */
  }
}

export function signOut() {
  try {
    sessionStorage.removeItem(SESSION_KEY);
  } catch {
    /* nothing to clear */
  }
}

export async function signIn(username, password) {
  let session;

  if (LOGIN_URL) {
    let res;
    try {
      res = await fetch(LOGIN_URL, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ username, password }),
      });
    } catch {
      throw new Error(
        "Can't reach the sign-in server. Check that it's running and allows requests from this page."
      );
    }
    if (res.status === 401 || res.status === 403) {
      throw new Error("That username or password isn't right.");
    }
    if (!res.ok) {
      throw new Error(`The sign-in server returned an error (${res.status}).`);
    }
    const data = await res.json().catch(() => ({}));
    session = { username, token: data.token || data.access_token || null };
  } else {
    if (!DEMO_USER || !DEMO_PASSWORD) {
      throw new Error(
        "Sign-in isn't set up yet. Add VITE_LOGIN_URL, or VITE_DEMO_USER and VITE_DEMO_PASSWORD, to .env and restart."
      );
    }
    if (username !== DEMO_USER || password !== DEMO_PASSWORD) {
      throw new Error("That username or password isn't right.");
    }
    session = { username, token: null };
  }

  saveSession(session);
  return session;
}
