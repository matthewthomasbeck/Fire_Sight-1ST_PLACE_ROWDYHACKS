// Map stream_N URLs to paired lidar_N / gps_N resource ids.

export function streamIndexFromUrl(url) {
  const match = String(url || "").match(/stream_(\d+)/i);
  return match ? Number(match[1]) : 1;
}

export function lidarIdFromStreamUrl(url) {
  return `lidar_${streamIndexFromUrl(url)}`;
}

export function gpsIdFromStreamUrl(url) {
  return `gps_${streamIndexFromUrl(url)}`;
}

export function apiUrl(path) {
  const base = (import.meta.env.VITE_API_BASE_URL || "").replace(/\/$/, "");
  if (!path.startsWith("/")) path = `/${path}`;
  return `${base}${path}`;
}
