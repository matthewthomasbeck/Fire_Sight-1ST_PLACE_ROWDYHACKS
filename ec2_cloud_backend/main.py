############################################################
# ec2_cloud_backend entrypoint. Wires pipelines; stays thin.
#
# Frontend flow:
#   Auth0 login, then home, then request stream_1/2 or gps_1/2
#
# Implementation order:
#   1. AUTH0    helpers/auth0.py, /api/v1/me
#   2. VIDEO    helpers/resources.py, video_pipeline.py, /api/v1/streams
#   3. GPS      helpers/resources.py, /api/v1/gps
#   4. WORKERS  helpers/workers.py (threads for 2 streams + 2 gps)
#   5. EDGE     helpers/edge_forwarder.py (optional)
############################################################

from __future__ import annotations

import logging
import os
from pathlib import Path

from dotenv import load_dotenv
from flask import Flask
from flask_cors import CORS

from api.gps import gps_bp
from api.health import health_bp
from api.ingest import ingest_bp
from api.me import me_bp
from api.streams import streams_bp
from helpers.auth0 import auth0_config_summary
from helpers.video_pipeline import init_socketio, socketio
from helpers.workers import start_workers

load_dotenv(Path(__file__).resolve().parent / ".env")


def setup_logging() -> None:
    level = getattr(logging, os.getenv("LOG_LEVEL", "INFO").upper(), logging.INFO)
    logging.basicConfig(
        level=level,
        format="%(asctime)s - %(levelname)s (%(name)s): %(message)s",
    )


def _log_auth0_config(logger: logging.Logger) -> None:
    """Log Auth0 domain/audience/issuer at startup (no secrets)."""
    try:
        cfg = auth0_config_summary()
        logger.info(
            "Auth0 user JWT: domain=%s audience=%s issuer=%s",
            cfg["domain"],
            cfg["audience"],
            cfg["issuer"],
        )
    except ValueError as exc:
        logger.error("Auth0 env incomplete: %s", exc)


def create_app() -> Flask:
    app = Flask(__name__)
    CORS(app, resources={r"/*": {"origins": os.getenv("CORS_ORIGINS", "*")}})

    app.register_blueprint(health_bp)
    app.register_blueprint(me_bp, url_prefix="/api/v1")
    app.register_blueprint(streams_bp, url_prefix="/api/v1")
    app.register_blueprint(gps_bp, url_prefix="/api/v1")
    app.register_blueprint(ingest_bp, url_prefix="/api/v1")

    init_socketio(app)
    return app


app = create_app()


def main() -> None:
    setup_logging()
    logger = logging.getLogger(__name__)
    _log_auth0_config(logger)

    start_workers()

    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", "8080"))
    logger.info("Starting ec2_cloud_backend on %s:%s", host, port)

    socketio.run(
        app,
        host=host,
        port=port,
        debug=os.getenv("FLASK_DEBUG", "false").lower() == "true",
        allow_unsafe_werkzeug=True,
    )


if __name__ == "__main__":
    main()
