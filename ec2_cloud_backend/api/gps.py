############################################################
# GPS resources. SPA requests gps_1 or gps_2, plus Maps key.
############################################################

import os

from flask import Blueprint, jsonify

from helpers.auth0 import require_auth
from helpers.resources import gps_ids, store

gps_bp = Blueprint("gps", __name__)


@gps_bp.route("/gps", methods=["GET"])
@require_auth
def list_gps():
    """List both firefighter GPS resources."""
    try:
        return jsonify(store.list_gps()), 200
    except NotImplementedError:
        return jsonify({
            "locations": [
                {"resource_id": gid, "available": False, "message": "GPS not available"}
                for gid in gps_ids()
            ],
            "message": "GPS pipeline not implemented",
        }), 200


@gps_bp.route("/gps/<gps_id>", methods=["GET"])
def get_gps(gps_id: str):
    """Return one GPS fix. Open like /mjpeg so the SPA can poll coords."""
    if gps_id not in gps_ids():
        return jsonify({"resource_id": gps_id, "available": False, "message": "Unknown GPS id"}), 404
    try:
        return jsonify(store.get_gps(gps_id)), 200
    except NotImplementedError:
        return jsonify({
            "resource_id": gps_id,
            "available": False,
            "lat": None,
            "lon": None,
            "message": "GPS not available",
        }), 200


@gps_bp.route("/maps/config", methods=["GET"])
@require_auth
def maps_config():
    """Google Maps JS key for authenticated SPA only."""
    key = (os.getenv("GOOGLE_MAPS_API_KEY") or "").strip()
    if not key:
        return jsonify({"configured": False, "googleMapsApiKey": None, "message": "Maps key not set"}), 503
    return jsonify({"configured": True, "googleMapsApiKey": key}), 200
