############################################################
# Pi to EC2 uplink (frame / GPS JSON).
############################################################

from flask import Blueprint, jsonify, request

from helpers.resources import gps_ids, store, stream_ids

ingest_bp = Blueprint("ingest", __name__)


@ingest_bp.route("/ingest/frame/jpeg", methods=["POST"])
def ingest_frame_jpeg():
    """
    Fast path: raw JPEG body.
    Query: stream_id=stream_1|stream_2
    Content-Type: image/jpeg
    """
    stream_id = (request.args.get("stream_id") or "").strip()
    if stream_id not in stream_ids():
        return jsonify({"status": "error", "message": "unknown stream_id"}), 400

    jpeg = request.get_data(cache=False)
    if not jpeg:
        return jsonify({"status": "error", "message": "empty jpeg body"}), 400

    try:
        store.set_stream_jpeg_bytes(stream_id, jpeg)
    except ValueError as exc:
        return jsonify({"status": "error", "message": str(exc)}), 400

    return jsonify({"status": "accepted", "stream_id": stream_id}), 202


@ingest_bp.route("/ingest/frame", methods=["POST"])
def ingest_frame():
    """
    Legacy body: { "stream_id": "stream_1"|"stream_2", "frame": "<base64 jpeg>" }
    Auth later: Auth0 M2M.
    """
    body = request.get_json(silent=True) or {}
    stream_id = body.get("stream_id")
    frame = body.get("frame")

    if stream_id not in stream_ids():
        return jsonify({"status": "error", "message": "unknown stream_id"}), 400
    if not frame or not isinstance(frame, str):
        return jsonify({"status": "error", "message": "frame required"}), 400

    try:
        store.set_stream_frame(stream_id, frame)
    except ValueError as exc:
        return jsonify({"status": "error", "message": str(exc)}), 400

    return jsonify({"status": "accepted", "stream_id": stream_id}), 202


@ingest_bp.route("/ingest/gps", methods=["POST"])
def ingest_gps():
    """
    Body: { "gps_id": "gps_1"|"gps_2", "lat": ..., "lon": ... }
    """
    body = request.get_json(silent=True) or {}
    gps_id = body.get("gps_id")
    if gps_id not in gps_ids():
        return jsonify({"status": "error", "message": "unknown gps_id"}), 400

    try:
        lat = float(body.get("lat"))
        lon = float(body.get("lon"))
    except (TypeError, ValueError):
        return jsonify({"status": "error", "message": "lat/lon required"}), 400

    try:
        store.set_gps(gps_id, lat, lon)
    except ValueError as exc:
        return jsonify({"status": "error", "message": str(exc)}), 400

    return jsonify({"status": "accepted", "gps_id": gps_id}), 202
