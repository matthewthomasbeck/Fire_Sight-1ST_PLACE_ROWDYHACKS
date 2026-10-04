############################################################
# Stream resources. SPA requests stream_1 or stream_2.
############################################################

from flask import Blueprint, Response, jsonify, stream_with_context

from helpers.auth0 import require_auth
from helpers.resources import store, stream_ids

streams_bp = Blueprint("streams", __name__)


@streams_bp.route("/streams", methods=["GET"])
@require_auth
def list_streams():
    return jsonify(store.list_streams()), 200


@streams_bp.route("/streams/<stream_id>", methods=["GET"])
@require_auth
def get_stream(stream_id: str):
    if stream_id not in stream_ids():
        return jsonify({"resource_id": stream_id, "available": False, "message": "Unknown stream"}), 404
    return jsonify(store.get_stream(stream_id)), 200


@streams_bp.route("/streams/<stream_id>/jpeg", methods=["GET"])
def get_stream_jpeg(stream_id: str):
    """
    Latest JPEG snapshot (legacy poll players).
    Prefer /mjpeg for live viewing.
    """
    if stream_id not in stream_ids():
        return jsonify({"resource_id": stream_id, "available": False, "message": "Unknown stream"}), 404

    jpeg = store.get_stream_jpeg_bytes(stream_id)
    if not jpeg:
        return Response(status=204)

    return Response(jpeg, mimetype="image/jpeg", headers={
        "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
        "Pragma": "no-cache",
    })


@streams_bp.route("/streams/<stream_id>/mjpeg", methods=["GET"])
def get_stream_mjpeg(stream_id: str):
    """
    Multipart MJPEG push. Browser <img src> updates without poll RTT.
    """
    if stream_id not in stream_ids():
        return jsonify({"resource_id": stream_id, "available": False, "message": "Unknown stream"}), 404

    boundary = b"firesightframe"

    @stream_with_context
    def generate():
        seq = 0
        # First frame immediately if present; then wait for newer seq.
        jpeg = store.get_stream_jpeg_bytes(stream_id)
        meta = store.get_stream(stream_id)
        seq = int(meta.get("seq") or 0)
        if jpeg:
            yield (
                b"--" + boundary + b"\r\n"
                b"Content-Type: image/jpeg\r\n"
                b"Content-Length: " + str(len(jpeg)).encode("ascii") + b"\r\n\r\n"
                + jpeg + b"\r\n"
            )

        while True:
            jpeg, seq = store.wait_stream_jpeg(stream_id, after_seq=seq, timeout_s=2.0)
            if not jpeg:
                continue
            yield (
                b"--" + boundary + b"\r\n"
                b"Content-Type: image/jpeg\r\n"
                b"Content-Length: " + str(len(jpeg)).encode("ascii") + b"\r\n\r\n"
                + jpeg + b"\r\n"
            )

    return Response(
        generate(),
        mimetype=f"multipart/x-mixed-replace; boundary={boundary.decode('ascii')}",
        headers={
            "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
            "Pragma": "no-cache",
            "X-Accel-Buffering": "no",
            "Connection": "keep-alive",
        },
    )
