############################################################
# Resource registry: stream_1, stream_2, gps_1, gps_2.
# Frontend requests a resource id; backend serves that slot.
############################################################

from __future__ import annotations

import os
import threading
from typing import Any, Dict, List, Optional


def stream_ids() -> List[str]:
    """Configured stream resource ids (default stream_1, stream_2)."""
    raw = (os.getenv("STREAM_IDS") or "stream_1,stream_2").strip()
    return [p.strip() for p in raw.split(",") if p.strip()]


def gps_ids() -> List[str]:
    """Configured GPS resource ids (default gps_1, gps_2)."""
    raw = (os.getenv("GPS_IDS") or "gps_1,gps_2").strip()
    return [p.strip() for p in raw.split(",") if p.strip()]


class ResourceStore:
    """Thread-safe latest value per resource id."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._streams: Dict[str, Optional[Any]] = {sid: None for sid in stream_ids()}
        self._gps: Dict[str, Optional[Dict[str, Any]]] = {gid: None for gid in gps_ids()}

    def set_stream_frame(self, stream_id: str, frame_b64: Optional[str]) -> None:
        """Store latest JPEG/base64 (or None if no stream)."""
        raise NotImplementedError("VIDEO")

    def get_stream(self, stream_id: str) -> Dict[str, Any]:
        """
        Return one stream resource.
        Shape: {resource_id, available, frame|null, message}
        """
        raise NotImplementedError("VIDEO")

    def list_streams(self) -> Dict[str, Any]:
        """Return status for all stream resources."""
        raise NotImplementedError("VIDEO")

    def set_gps(self, gps_id: str, lat: float, lon: float, **extra: Any) -> None:
        """Store latest GPS JSON for gps_1 or gps_2."""
        raise NotImplementedError("GPS")

    def get_gps(self, gps_id: str) -> Dict[str, Any]:
        """
        Return one GPS resource.
        Shape: {resource_id, available, lat, lon, message}
        """
        raise NotImplementedError("GPS")

    def list_gps(self) -> Dict[str, Any]:
        """Return status for all GPS resources."""
        raise NotImplementedError("GPS")


store = ResourceStore()
