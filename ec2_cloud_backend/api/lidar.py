############################################################
# Lidar resources. SPA/debug can poll lidar_1 or lidar_2.
############################################################

from flask import Blueprint, jsonify

from helpers.auth0 import require_auth
from helpers.resources import lidar_ids, store

lidar_bp = Blueprint("lidar", __name__)


@lidar_bp.route("/lidar", methods=["GET"])
@require_auth
def list_lidar():
    """List lidar scan resources."""
    return jsonify(store.list_lidar()), 200


@lidar_bp.route("/lidar/<lidar_id>", methods=["GET"])
def get_lidar(lidar_id: str):
    """Return one lidar scan. Open like /mjpeg so the SPA <canvas> can poll."""
    if lidar_id not in lidar_ids():
        return jsonify(
            {"resource_id": lidar_id, "available": False, "message": "Unknown lidar id"}
        ), 404
    return jsonify(store.get_lidar(lidar_id)), 200
