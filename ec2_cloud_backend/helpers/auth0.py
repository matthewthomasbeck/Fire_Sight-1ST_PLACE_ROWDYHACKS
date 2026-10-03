############################################################
# Auth0 helpers.
# Port from Solana_Soothsayer/application_backend/helpers/auth0.py
############################################################

from __future__ import annotations

import logging
import os
from functools import wraps
from pathlib import Path
from typing import Any, Dict, Optional

import jwt
import requests
from dotenv import load_dotenv
from flask import jsonify, request
from jwt import PyJWKClient

load_dotenv(Path(__file__).resolve().parent.parent / ".env")

logger = logging.getLogger(__name__)


def _auth0_domain() -> str:
    """Return normalized AUTH0_DOMAIN (no scheme)."""
    raise NotImplementedError("AUTH0: implement from Soothsayer application_backend/helpers/auth0.py")


def _auth0_user_audience() -> str:
    """Return AUTH0_AUDIENCE for dispatch SPA user tokens."""
    raise NotImplementedError("AUTH0")


def _auth0_issuer() -> str:
    """Return expected JWT issuer (https://{domain}/)."""
    raise NotImplementedError("AUTH0")


def get_jwks_client(force_refresh: bool = False) -> PyJWKClient:
    """Cached PyJWKClient for Auth0 JWKS."""
    raise NotImplementedError("AUTH0")


def validate_user_token(token: str) -> Dict[str, Any]:
    """Validate SPA Bearer JWT and return claims. Soothsayer parity."""
    raise NotImplementedError("AUTH0")


def require_auth(f):
    """Flask decorator: require Authorization Bearer user JWT."""

    @wraps(f)
    def decorated(*args, **kwargs):
        # AUTH0: validate header, set g.user_claims / g.user_sub
        return jsonify({"error": "Unauthorized", "message": "Auth0 not implemented"}), 401

    return decorated


def get_m2m_token(force_refresh: bool = False) -> str:
    """Client-credentials token for edge or Pi calls (optional)."""
    raise NotImplementedError("AUTH0/edge: Soothsayer get_m2m_token")
