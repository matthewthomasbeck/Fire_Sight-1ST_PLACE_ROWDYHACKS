############################################################
# Resource registry: stream_1, stream_2, gps_1, gps_2.
############################################################

from __future__ import annotations

import base64
import os
import threading
import time
from typing import Any, Dict, List, Optional


def stream_ids() -> List[str]:
    raw = (os.getenv("STREAM_IDS") or "stream_1,stream_2").strip()
    return [p.strip() for p in raw.split(",") if p.strip()]


def gps_ids() -> List[str]:
    raw = (os.getenv("GPS_IDS") or "gps_1,gps_2").strip()
    return [p.strip() for p in raw.split(",") if p.strip()]


class ResourceStore:
    """Thread-safe latest value per resource id."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._streams: Dict[str, Optional[str]] = {sid: None for sid in stream_ids()}
        self._stream_ts_ms: Dict[str, Optional[int]] = {sid: None for sid in stream_ids()}
        self._gps: Dict[str, Optional[Dict[str, Any]]] = {gid: None for gid in gps_ids()}

    def set_stream_frame(self, stream_id: str, frame_b64: Optional[str]) -> None:
        if stream_id not in self._streams:
            raise ValueError(f"unknown stream_id: {stream_id}")
        if frame_b64 is not None:
            frame_b64 = frame_b64.strip()
            if frame_b64.startswith("data:"):
                # allow data:image/jpeg;base64,... payloads
                comma = frame_b64.find(",")
                if comma >= 0:
                    frame_b64 = frame_b64[comma + 1 :]
            if not frame_b64:
                frame_b64 = None
        with self._lock:
            self._streams[stream_id] = frame_b64
            self._stream_ts_ms[stream_id] = int(time.time() * 1000) if frame_b64 else None

    def get_stream(self, stream_id: str) -> Dict[str, Any]:
        if stream_id not in self._streams:
            return {
                "resource_id": stream_id,
                "available": False,
                "frame": None,
                "message": "Unknown stream",
            }
        with self._lock:
            frame = self._streams.get(stream_id)
            ts = self._stream_ts_ms.get(stream_id)
        available = frame is not None
        return {
            "resource_id": stream_id,
            "available": available,
            "frame": frame,
            "updated_at_ms": ts,
            "message": "ok" if available else "No stream available",
        }

    def list_streams(self) -> Dict[str, Any]:
        return {"streams": [self.get_stream(sid) for sid in stream_ids()]}

    def get_stream_jpeg_bytes(self, stream_id: str) -> Optional[bytes]:
        meta = self.get_stream(stream_id)
        frame = meta.get("frame")
        if not frame:
            return None
        try:
            return base64.b64decode(frame, validate=False)
        except Exception:
            return None

    def set_gps(self, gps_id: str, lat: float, lon: float, **extra: Any) -> None:
        if gps_id not in self._gps:
            raise ValueError(f"unknown gps_id: {gps_id}")
        payload = {"resource_id": gps_id, "available": True, "lat": lat, "lon": lon, **extra}
        with self._lock:
            self._gps[gps_id] = payload

    def get_gps(self, gps_id: str) -> Dict[str, Any]:
        if gps_id not in self._gps:
            return {
                "resource_id": gps_id,
                "available": False,
                "lat": None,
                "lon": None,
                "message": "Unknown GPS id",
            }
        with self._lock:
            row = self._gps.get(gps_id)
        if not row:
            return {
                "resource_id": gps_id,
                "available": False,
                "lat": None,
                "lon": None,
                "message": "GPS not available",
            }
        return {**row, "message": "ok"}

    def list_gps(self) -> Dict[str, Any]:
        return {"locations": [self.get_gps(gid) for gid in gps_ids()]}


store = ResourceStore()
