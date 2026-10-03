#pragma once

#include <string>

#include "config/Config.hpp"

// Auth0 client-credentials for ingest (Soothsayer get_m2m_token shape).

struct Auth0M2M {
    bool init(const Config& cfg);
    void shutdown();

    bool get_access_token(std::string& token_out, bool force_refresh = false);
    bool enabled() const { return enabled_; }

private:
    bool enabled_ = false;
    std::string domain_;
    std::string client_id_;
    std::string client_secret_;
    std::string audience_;
    std::string cached_token_;
    double expires_at_unix_ = 0.0;
};
