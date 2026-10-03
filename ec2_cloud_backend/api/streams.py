############################################################
# Stream resources. SPA requests stream_1 or stream_2.
############################################################

from flask import Blueprint, jsonify

from helpers.auth0 import require_auth
from helpers.resources import store, stream_ids

streams_bp = Blueprint("streams", __name__)


@streams_bp.route("/streams", methods=["GET"])
@require_auth
def list_streams():
    """List stream resources and live status."""
    try:
        return jsonify(store.list_streams()), 200
    except NotImplementedError:
        return jsonify({
            "streams": [
                {"resource_id": sid, "available": False, "message": "No stream available"}
                for sid in stream_ids()
            ],
            "message": "Video pipeline not implemented",
        }), 200


@streams_bp.route("/streams/<stream_id>", methods=["GET"])
@require_auth
def get_stream(stream_id: str):
    """Return one stream resource, or no-stream status."""
    if stream_id not in stream_ids():
        return jsonify({"resource_id": stream_id, "available": False, "message": "Unknown stream"}), 404
    try:
        return jsonify(store.get_stream(stream_id)), 200
    except NotImplementedError:
        return jsonify({
            "resource_id": stream_id,
            "available": False,
            "frame": None,
            "message": "No stream available",
        }), 200
