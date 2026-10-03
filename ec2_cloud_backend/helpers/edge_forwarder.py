############################################################
# Optional: forward frames to nvidia_edge
# (Soothsayer predictor_client pattern). Lowest priority.
############################################################

from __future__ import annotations

import os
from typing import Any, Dict, Optional


def is_enabled() -> bool:
    return os.getenv("EDGE_FORWARD_ENABLED", "false").lower() == "true"


def forward_frame(stream_id: str, frame_b64: str, **kwargs: Any) -> Optional[Dict[str, Any]]:
    """POST frame to EDGE_BASE_URL with M2M Bearer. No-op when disabled."""
    if not is_enabled():
        return None
    raise NotImplementedError("EDGE: mirror Soothsayer prediction_client")
