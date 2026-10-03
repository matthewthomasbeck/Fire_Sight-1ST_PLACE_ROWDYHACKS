############################################################
# Pi to EC2 uplink stubs (frame / GPS JSON).
############################################################

from flask import Blueprint, jsonify, request

from helpers.resources import store

ingest_bp = Blueprint("ingest", __name__)


@ingest_bp.route("/ingest/frame", methods=["POST"])
def ingest_frame():
    """
    Body: { "stream_id": "stream_1"|"stream_2", "frame": "<base64 jpeg>" }
    Auth later: Auth0 M2M (Soothsayer predictor style).
    """
    body = request.get_json(silent=True) or {}
    stream_id = body.get("stream_id")
    frame = body.get("frame")
    try:
        store.set_stream_frame(stream_id, frame)
        return jsonify({"status": "accepted", "stream_id": stream_id}), 202
    except NotImplementedError:
        return jsonify({"status": "not_implemented", "component": "VIDEO/INGEST"}), 501


@ingest_bp.route("/ingest/gps", methods=["POST"])
def ingest_gps():
    """
    Body: { "gps_id": "gps_1"|"gps_2", "lat": ..., "lon": ... }
    """
    body = request.get_json(silent=True) or {}
    gps_id = body.get("gps_id")
    try:
        store.set_gps(gps_id, body.get("lat"), body.get("lon"))
        return jsonify({"status": "accepted", "gps_id": gps_id}), 202
    except NotImplementedError:
        return jsonify({"status": "not_implemented", "component": "GPS/INGEST"}), 501
