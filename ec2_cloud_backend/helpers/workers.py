############################################################
# Background threads: 2 stream uplinks + 2 GPS uplinks.
############################################################

from __future__ import annotations

import logging
import threading
from typing import List

from helpers.resources import gps_ids, stream_ids

logger = logging.getLogger(__name__)

_threads: List[threading.Thread] = []
_stop = threading.Event()


def _stream_worker(stream_id: str) -> None:
    """
    Background loop for one video stream resource.
    Pull or receive frames, then store.set_stream_frame(stream_id, ...).
    """
    raise NotImplementedError(f"WORKERS/VIDEO: stream worker for {stream_id}")


def _gps_worker(gps_id: str) -> None:
    """
    Background loop for one GPS resource.
    Receive JSON coords, then store.set_gps(gps_id, lat, lon).
    """
    raise NotImplementedError(f"WORKERS/GPS: gps worker for {gps_id}")


def start_workers() -> None:
    """Start one thread per stream_1/2 and gps_1/2. Called from main."""
    logger.info(
        "Workers not started (skeleton). Planned streams=%s gps=%s",
        stream_ids(),
        gps_ids(),
    )


def stop_workers() -> None:
    """Signal worker threads to exit."""
    _stop.set()
