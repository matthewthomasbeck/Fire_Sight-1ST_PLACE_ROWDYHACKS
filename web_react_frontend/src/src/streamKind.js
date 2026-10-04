// Decides how a stream address should be played.
//   ws:// or wss://            -> "ws"    (one image per WebSocket message)
//   http(s):// or a /path      -> "mjpeg" (a continuous image feed over HTTP)
//   anything else              -> null    (not playable)
export function streamKind(url) {
  if (typeof url !== "string") return null;
  if (/^wss?:\/\//i.test(url)) return "ws";
  if (/^https?:\/\//i.test(url) || url.startsWith("/")) return "mjpeg";
  return null;
}
