############################################################
############### IMPORT / CREATE DEPENDENCIES ###############
############################################################


########## IMPORT DEPENDENCIES ##########

##### import necessary libraries #####

from __future__ import annotations # enable postponed evaluation of type hints

import logging # import logging for Auth0 helper messages
import os # import os for Auth0 env vars
import time # import time for M2M token expiry cache
from functools import wraps # import wraps to preserve Flask view metadata
from pathlib import Path # resolve .env next to ec2_cloud_backend
from typing import Optional # import typing helpers

##### import third-party libraries #####

import jwt # import PyJWT for RS256 access-token verification
import requests # import requests for M2M token endpoint calls
from dotenv import load_dotenv # import dotenv to load .env
from flask import jsonify, request # import Flask helpers used by require_auth
from jwt import PyJWKClient # import PyJWKClient for Auth0 JWKS key lookup

##### load environment #####

load_dotenv(Path(__file__).resolve().parent.parent / ".env") # load Auth0-related environment variables





##################################################
############### MODULE STATE #####################
##################################################


########## CREATE LOGGER / TOKEN CACHE ##########

logger = logging.getLogger(__name__) # create module logger

_m2m_token: Optional[str] = None # cached Auth0 M2M access token for edge calls
_m2m_token_expires_at: float = 0.0 # unix expiry time for cached M2M token
_jwks_client: Optional[PyJWKClient] = None # cached PyJWKClient for user JWT verification
_jwks_client_domain: Optional[str] = None # domain the cached client was built for
_JWKS_TTL_SECONDS = 3600 # refresh JWKS client hourly





##################################################
############### AUTH0 HELPERS ####################
##################################################


########## AUTH0 CONFIG SUMMARY ##########

def auth0_config_summary(): # function to return domain/audience/issuer for startup logs

    return {
        "domain": _auth0_domain(),
        "audience": _auth0_user_audience(),
        "issuer": _auth0_issuer(),
    } # no secrets


########## AUTH0 DOMAIN ##########

def _auth0_domain(): # function to read and normalize AUTH0_DOMAIN from environment

    domain = (os.getenv("AUTH0_DOMAIN") or "").strip().rstrip("/") # strip whitespace / slash
    if domain.startswith("https://"): # allow full issuer-style values
        domain = domain[len("https://"):] # keep host only
    if domain.startswith("http://"): # allow http form
        domain = domain[len("http://"):] # keep host only
    if not domain: # domain required for JWKS / token URL
        raise ValueError("AUTH0_DOMAIN is not set") # fail fast
    return domain # normalized tenant domain


########## AUTH0 USER AUDIENCE ##########

def _auth0_user_audience(): # function to read dispatch SPA user API audience

    audience = (os.getenv("AUTH0_AUDIENCE") or "").strip() # expected aud claim
    if not audience: # audience required
        raise ValueError("AUTH0_AUDIENCE is not set") # fail fast
    return audience # e.g. https://rowdyhacks.dispatch.api


########## AUTH0 ISSUER ##########

def _auth0_issuer(): # function to read expected JWT issuer (defaults to https://{domain}/)

    issuer = (os.getenv("AUTH0_ISSUER") or "").strip() # optional explicit issuer
    if issuer: # configured
        return issuer if issuer.endswith("/") else f"{issuer}/" # Auth0 issuer has trailing slash
    return f"https://{_auth0_domain()}/" # standard Auth0 issuer


########## GET JWKS CLIENT ##########

def get_jwks_client(force_refresh=False): # function to return a cached PyJWKClient for Auth0 JWKS

    global _jwks_client, _jwks_client_domain # mutate module cache

    domain = _auth0_domain() # Auth0 tenant host
    if (
        force_refresh
        or _jwks_client is None
        or _jwks_client_domain != domain
    ): # rebuild client when missing / domain changed
        jwks_url = f"https://{domain}/.well-known/jwks.json" # JWKS endpoint
        logger.info("Creating Auth0 JWKS client for %s", jwks_url) # log once per rebuild
        _jwks_client = PyJWKClient(jwks_url, cache_keys=True, lifespan=_JWKS_TTL_SECONDS) # cached keys
        _jwks_client_domain = domain # remember domain

    return _jwks_client # ready client


########## GET JWKS ##########

def get_jwks(force_refresh=False): # function to fetch Auth0 JWKS used to verify SPA user JWTs

    client = get_jwks_client(force_refresh=force_refresh) # ensure client exists
    domain = _auth0_domain() # tenant
    url = f"https://{domain}/.well-known/jwks.json" # JWKS URL
    response = requests.get(url, timeout=10) # GET public keys
    response.raise_for_status() # raise on HTTP error
    return response.json() # JWKS document


########## VALIDATE USER TOKEN ##########

def validate_user_token(token): # function to validate SPA Bearer token and return JWT claims

    if not token: # empty bearer
        raise ValueError("Missing access token") # reject

    audience = _auth0_user_audience() # dispatch API audience
    issuer = _auth0_issuer() # expected iss
    client = get_jwks_client() # JWKS-backed key client

    try:
        signing_key = client.get_signing_key_from_jwt(token) # match kid to public key
        claims = jwt.decode(
            token,
            signing_key.key,
            algorithms=["RS256"],
            audience=audience,
            issuer=issuer,
            options={"require": ["exp", "iat", "sub"]},
        ) # verified claims
    except jwt.PyJWTError as exc: # signature / aud / iss / exp failures
        logger.warning("User JWT validation failed: %s", exc) # no token logged
        raise ValueError(f"Invalid access token: {exc}") from exc # surface to decorator

    logger.debug(
        "User token accepted (sub=%s aud=%s)",
        claims.get("sub"),
        claims.get("aud"),
    ) # success without dumping token
    return claims # verified claims for request context


########## REQUIRE AUTH ##########

def require_auth(f): # decorator to require Authorization Bearer user token on Flask routes

    @wraps(f) # preserve original view function name/docs
    def decorated(*args, **kwargs): # wrapped view that enforces Auth0 user auth

        auth_header = request.headers.get("Authorization", "") # read Authorization header
        if not auth_header.startswith("Bearer "): # require Bearer scheme
            return jsonify({
                "error": "Unauthorized",
                "message": "Missing or invalid Authorization header",
            }), 401 # reject missing/malformed header

        token = auth_header.removeprefix("Bearer ").strip() # extract access token
        try:
            claims = validate_user_token(token) # verify RS256 JWT via Auth0 JWKS
        except Exception as e: # validation failure
            logger.warning("User token validation failed: %s", e) # log reason
            return jsonify({
                "error": "Unauthorized",
                "message": "Invalid access token",
            }), 401 # reject invalid token

        from flask import g # attach claims to request context
        g.user_claims = claims # full claim set
        g.user_sub = claims.get("sub") # subject shortcut
        return f(*args, **kwargs) # proceed to protected view

    return decorated # return wrapped Flask view


########## GET M2M TOKEN ##########

def get_m2m_token(force_refresh=False): # function to get Auth0 client-credentials token for edge services

    global _m2m_token, _m2m_token_expires_at # mutate module token cache

    now = time.time() # current unix time
    if (
        not force_refresh
        and _m2m_token
        and now < (_m2m_token_expires_at - 60)
    ): # reuse cached token with 60s skew
        return _m2m_token # cached access token

    domain = _auth0_domain() # Auth0 tenant
    client_id = os.getenv("AUTH0_M2M_CLIENT_ID") # M2M client id
    client_secret = os.getenv("AUTH0_M2M_CLIENT_SECRET") # M2M client secret
    audience = os.getenv("AUTH0_PREDICTOR_AUDIENCE") # edge API audience

    if not all([client_id, client_secret, audience]): # all three required
        raise ValueError(
            "AUTH0_M2M_CLIENT_ID, AUTH0_M2M_CLIENT_SECRET, and "
            "AUTH0_PREDICTOR_AUDIENCE must be set"
        ) # fail fast on missing M2M config

    token_url = f"https://{domain}/oauth/token" # Auth0 token endpoint
    payload = {
        "grant_type": "client_credentials",
        "client_id": client_id,
        "client_secret": client_secret,
        "audience": audience,
    } # client-credentials body

    logger.info("Requesting Auth0 M2M token for edge audience") # log token request
    response = requests.post(token_url, json=payload, timeout=15) # POST for access token
    response.raise_for_status() # raise on HTTP error
    data = response.json() # token response JSON

    _m2m_token = data["access_token"] # cache access token
    _m2m_token_expires_at = now + float(data.get("expires_in", 3600)) # cache expiry
    return _m2m_token # fresh M2M token
