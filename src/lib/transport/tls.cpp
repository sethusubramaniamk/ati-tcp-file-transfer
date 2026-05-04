#include "ftx/transport/tls.hpp"

#include <stdexcept>

namespace ftx::transport {

namespace {

void apply_common_(asio::ssl::context& ctx, const TlsConfig& cfg) {
    // TLS 1.3 only — no negotiation down to weaker versions.
    ctx.set_options(asio::ssl::context::default_workarounds |
                    asio::ssl::context::no_sslv2 |
                    asio::ssl::context::no_sslv3 |
                    asio::ssl::context::no_tlsv1 |
                    asio::ssl::context::no_tlsv1_1 |
                    asio::ssl::context::no_tlsv1_2 |
                    asio::ssl::context::single_dh_use);

    if (!cfg.cert_path.empty()) {
        ctx.use_certificate_chain_file(cfg.cert_path);
    }
    if (!cfg.key_path.empty()) {
        ctx.use_private_key_file(cfg.key_path, asio::ssl::context::pem);
    }
    if (!cfg.ca_path.empty()) {
        ctx.load_verify_file(cfg.ca_path);
    }
}

}  // namespace

std::unique_ptr<asio::ssl::context> make_server_tls_context(const TlsConfig& cfg) {
    auto ctx = std::make_unique<asio::ssl::context>(asio::ssl::context::tlsv13_server);
    apply_common_(*ctx, cfg);

    if (cfg.verify_peer) {
        // mTLS — every client must present a cert chained to our CA.
        ctx->set_verify_mode(asio::ssl::verify_peer |
                             asio::ssl::verify_fail_if_no_peer_cert);
    } else {
        ctx->set_verify_mode(asio::ssl::verify_none);
    }
    return ctx;
}

std::unique_ptr<asio::ssl::context> make_client_tls_context(const TlsConfig& cfg) {
    auto ctx = std::make_unique<asio::ssl::context>(asio::ssl::context::tlsv13_client);
    apply_common_(*ctx, cfg);

    if (cfg.verify_peer) {
        ctx->set_verify_mode(asio::ssl::verify_peer);
        if (!cfg.sni_host.empty()) {
            ctx->set_verify_callback(asio::ssl::host_name_verification(cfg.sni_host));
        }
    } else {
        ctx->set_verify_mode(asio::ssl::verify_none);
    }
    return ctx;
}

}  // namespace ftx::transport
