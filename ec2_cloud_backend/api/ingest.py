############################################################
# Pi to EC2 uplink (frame / GPS / lidar JSON).
############################################################

from flask import Blueprint, jsonify, request

from helpers.resources import gps_ids, lidar_ids, store, stream_ids

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


@ingest_bp.route("/ingest/lidar", methods=["POST"])
def ingest_lidar():
    """
    Body: {
      "lidar_id": "lidar_1"|"lidar_2",
      "bin_count": 720,
      "valid_bins": int,
      "speed_dps": float,
      "distance_mm": [uint16...],
      "intensity": [uint8...]
    }
    """
    body = request.get_json(silent=True) or {}
    lidar_id = body.get("lidar_id")
    if lidar_id not in lidar_ids():
        return jsonify({"status": "error", "message": "unknown lidar_id"}), 400

    distance_mm = body.get("distance_mm")
    intensity = body.get("intensity")
    if not isinstance(distance_mm, list) or not isinstance(intensity, list):
        return jsonify({"status": "error", "message": "distance_mm/intensity arrays required"}), 400

    try:
        bin_count = int(body.get("bin_count") or len(distance_mm))
        valid_bins = int(body.get("valid_bins") or 0)
        speed_dps = float(body.get("speed_dps") or 0.0)
    except (TypeError, ValueError):
        return jsonify({"status": "error", "message": "bin_count/valid_bins/speed_dps invalid"}), 400

    if bin_count <= 0 or len(distance_mm) < bin_count or len(intensity) < bin_count:
        return jsonify({"status": "error", "message": "scan arrays shorter than bin_count"}), 400

    try:
        store.set_lidar(
            lidar_id,
            {
                "bin_count": bin_count,
                "valid_bins": valid_bins,
                "speed_dps": speed_dps,
                "distance_mm": distance_mm[:bin_count],
                "intensity": intensity[:bin_count],
            },
        )
    except ValueError as exc:
        return jsonify({"status": "error", "message": str(exc)}), 400

    return jsonify({"status": "accepted", "lidar_id": lidar_id}), 202
