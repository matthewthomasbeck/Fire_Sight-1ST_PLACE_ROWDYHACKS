#include "net/Auth0M2M.hpp"

#include "util/Log.hpp"

bool Auth0M2M::init(const Config& cfg) {
    domain_ = cfg.auth0_domain;
    client_id_ = cfg.auth0_m2m_client_id;
    client_secret_ = cfg.auth0_m2m_client_secret;
    audience_ = cfg.auth0_edge_audience;
    enabled_ = !domain_.empty() && !client_id_.empty() && !client_secret_.empty();
    if (!enabled_) {
        logutil::warn("Auth0M2M disabled (missing env)");
    }
    return true;
}

void Auth0M2M::shutdown() {
    cached_token_.clear();
    enabled_ = false;
}

bool Auth0M2M::get_access_token(std::string& token_out, bool /*force_refresh*/) {
    if (!enabled_) {
        token_out.clear();
        return false;
    }
    logutil::warn("Auth0M2M::get_access_token not implemented");
    token_out.clear();
    return false;
}
