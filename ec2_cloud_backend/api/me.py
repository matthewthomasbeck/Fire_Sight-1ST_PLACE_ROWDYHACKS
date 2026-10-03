############################################################
# GET /api/v1/me. Auth0 smoke test for the dispatch SPA.
############################################################

from flask import Blueprint, g, jsonify

from helpers.auth0 import require_auth

me_bp = Blueprint("me", __name__)


@me_bp.route("/me", methods=["GET"])
@require_auth
def me():
    """Return Auth0 sub for the logged-in dispatcher."""
    return jsonify({
        "sub": getattr(g, "user_sub", None),
        "claims": getattr(g, "user_claims", None),
    }), 200
