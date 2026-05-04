#pragma once

#include <asio/ssl.hpp>

#include <memory>
#include <string>

namespace ftx::transport {

// Paths to PEM-encoded materials. Pass empty optional/string to skip a step.
struct TlsConfig {
    std::string cert_path;   ///< this side's certificate (PEM, may be a chain)
    std::string key_path;    ///< this side's private key  (PEM)
    std::string ca_path;     ///< trust anchor for peer verification (PEM bundle)
    bool        verify_peer = true;  ///< mTLS when true on the server
    std::string sni_host;    ///< client only — hostname for SNI + name check
};

// Build an asio ssl_context configured for TLS 1.3 only (with TLS 1.2 fallback
// disabled), the supplied certificate/key, and optional peer verification.
[[nodiscard]] std::unique_ptr<asio::ssl::context>
make_server_tls_context(const TlsConfig& cfg);

[[nodiscard]] std::unique_ptr<asio::ssl::context>
make_client_tls_context(const TlsConfig& cfg);

}  // namespace ftx::transport
