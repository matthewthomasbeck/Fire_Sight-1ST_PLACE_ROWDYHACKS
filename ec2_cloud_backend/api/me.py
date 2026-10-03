############################################################
############### IMPORT / CREATE DEPENDENCIES ###############
############################################################


########## IMPORT DEPENDENCIES ##########

##### import third-party libraries #####

from flask import Blueprint, g, jsonify # import Flask request/response helpers

##### import local modules #####

from helpers.auth0 import require_auth # import Auth0 bearer-token decorator for SPA users





##################################################
############### ME BLUEPRINT #####################
##################################################


########## CREATE BLUEPRINT ##########

me_bp = Blueprint("me", __name__) # create blueprint for /api/v1/me





##################################################
############### AUTH ROUTES ######################
##################################################


########## ME ##########

@me_bp.route("/me", methods=["GET"]) # register GET /api/v1/me
@require_auth # require dispatch SPA Auth0 access token
def me(): # function to return Auth0 sub/claims for the logged-in dispatcher

    return jsonify({
        "sub": getattr(g, "user_sub", None),
        "claims": getattr(g, "user_claims", {}),
    }), 200 # Auth0 identity for React SPA
