############################################################
# Backend to frontend video delivery (MTB-style frame fanout).
# Socket auth uses Auth0 user JWT (not Cognito).
############################################################

from __future__ import annotations

import logging
from typing import Optional

from flask_socketio import SocketIO

logger = logging.getLogger(__name__)

socketio = SocketIO(cors_allowed_origins="*", async_mode="threading")


def init_socketio(app) -> SocketIO:
    """Attach Socket.IO to Flask app. Called from main.create_app()."""
    socketio.init_app(app)
    register_handlers(socketio)
    return socketio


def register_handlers(sio: SocketIO) -> None:
    """
    Wire signaling events after Auth0 login:
      client emits auth { token, resource_id? }
      server emits auth-success or auth-failed
      server emits stream-available, stream-unavailable, or video-frame
    """
    logger.info("Socket.IO handlers not registered (skeleton)")


def emit_frame(stream_id: str, frame_b64: str, timestamp_ms: Optional[int] = None) -> None:
    """Push one frame to viewers subscribed to stream_id."""
    raise NotImplementedError("VIDEO")


def emit_stream_status(stream_id: str) -> None:
    """Emit stream-available or stream-unavailable for subscribers."""
    raise NotImplementedError("VIDEO")
