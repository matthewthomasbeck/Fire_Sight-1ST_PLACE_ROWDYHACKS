############################################################
# Resource registry: stream_1, stream_2, gps_1, gps_2.
############################################################

from __future__ import annotations

import base64
import os
import threading
import time
from typing import Any, Dict, List, Optional, Tuple


def stream_ids() -> List[str]:
    raw = (os.getenv("STREAM_IDS") or "stream_1,stream_2").strip()
    return [p.strip() for p in raw.split(",") if p.strip()]


def gps_ids() -> List[str]:
    raw = (os.getenv("GPS_IDS") or "gps_1,gps_2").strip()
    return [p.strip() for p in raw.split(",") if p.strip()]


class ResourceStore:
    """Thread-safe latest value per resource id. Streams store raw JPEG bytes."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._cv = threading.Condition(self._lock)
        self._streams: Dict[str, Optional[bytes]] = {sid: None for sid in stream_ids()}
        self._stream_ts_ms: Dict[str, Optional[int]] = {sid: None for sid in stream_ids()}
        self._stream_seq: Dict[str, int] = {sid: 0 for sid in stream_ids()}
        self._gps: Dict[str, Optional[Dict[str, Any]]] = {gid: None for gid in gps_ids()}

    def set_stream_jpeg_bytes(self, stream_id: str, jpeg: Optional[bytes]) -> None:
        if stream_id not in self._streams:
            raise ValueError(f"unknown stream_id: {stream_id}")
        if jpeg is not None and len(jpeg) == 0:
            jpeg = None
        with self._cv:
            self._streams[stream_id] = jpeg
            self._stream_ts_ms[stream_id] = int(time.time() * 1000) if jpeg else None
            if jpeg is not None:
                self._stream_seq[stream_id] = self._stream_seq.get(stream_id, 0) + 1
            self._cv.notify_all()

    def set_stream_frame(self, stream_id: str, frame_b64: Optional[str]) -> None:
        """Legacy JSON ingest: decode base64 once, store raw JPEG bytes."""
        if stream_id not in self._streams:
            raise ValueError(f"unknown stream_id: {stream_id}")
        jpeg: Optional[bytes] = None
        if frame_b64 is not None:
            frame_b64 = frame_b64.strip()
            if frame_b64.startswith("data:"):
                comma = frame_b64.find(",")
                if comma >= 0:
                    frame_b64 = frame_b64[comma + 1 :]
            if frame_b64:
                try:
                    jpeg = base64.b64decode(frame_b64, validate=False)
                except Exception as exc:
                    raise ValueError(f"invalid base64 frame: {exc}") from exc
                if not jpeg:
                    jpeg = None
        self.set_stream_jpeg_bytes(stream_id, jpeg)

    def get_stream(self, stream_id: str) -> Dict[str, Any]:
        if stream_id not in self._streams:
            return {
                "resource_id": stream_id,
                "available": False,
                "frame": None,
                "message": "Unknown stream",
            }
        with self._lock:
            jpeg = self._streams.get(stream_id)
            ts = self._stream_ts_ms.get(stream_id)
            seq = self._stream_seq.get(stream_id, 0)
        available = jpeg is not None
        frame_b64 = base64.b64encode(jpeg).decode("ascii") if jpeg else None
        return {
            "resource_id": stream_id,
            "available": available,
            "frame": frame_b64,
            "updated_at_ms": ts,
            "seq": seq,
            "message": "ok" if available else "No stream available",
        }

    def list_streams(self) -> Dict[str, Any]:
        return {"streams": [self.get_stream(sid) for sid in stream_ids()]}

    def get_stream_jpeg_bytes(self, stream_id: str) -> Optional[bytes]:
        if stream_id not in self._streams:
            return None
        with self._lock:
            return self._streams.get(stream_id)

    def wait_stream_jpeg(
        self, stream_id: str, after_seq: int, timeout_s: float = 1.0
    ) -> Tuple[Optional[bytes], int]:
        """Block until a newer frame than after_seq arrives (or timeout)."""
        if stream_id not in self._streams:
            return None, after_seq
        deadline = time.time() + timeout_s
        with self._cv:
            while self._stream_seq.get(stream_id, 0) <= after_seq:
                remaining = deadline - time.time()
                if remaining <= 0:
                    break
                self._cv.wait(timeout=remaining)
            jpeg = self._streams.get(stream_id)
            seq = self._stream_seq.get(stream_id, 0)
            return jpeg, seq

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
