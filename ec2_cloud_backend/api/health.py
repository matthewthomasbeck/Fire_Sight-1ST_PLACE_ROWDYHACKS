############################################################
# Health / root. No auth.
############################################################

from flask import Blueprint, jsonify

health_bp = Blueprint("health", __name__)


@health_bp.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "healthy", "service": "ec2_cloud_backend"}), 200


@health_bp.route("/", methods=["GET"])
def root():
    return jsonify({
        "service": "ec2_cloud_backend",
        "flow": "Auth0 login, home, then stream_1|stream_2|gps_1|gps_2",
        "endpoints": {
            "health": "/health",
            "me": "/api/v1/me",
            "streams": "/api/v1/streams",
            "stream": "/api/v1/streams/<stream_id>",
            "gps": "/api/v1/gps",
            "gps_one": "/api/v1/gps/<gps_id>",
            "maps": "/api/v1/maps/config",
            "ingest": "POST /api/v1/ingest",
        },
    }), 200
