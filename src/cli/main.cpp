#include <CLI/CLI.hpp>
#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "ftx/transport/client.hpp"
#include "ftx/transport/server.hpp"
#include "ftx/transport/tls.hpp"
#include "ftx/version.hpp"

namespace {

struct CommonTlsArgs {
    std::string cert;
    std::string key;
    std::string ca;
    bool        no_verify_peer = false;
    std::string sni;
    bool        insecure = false;  // disable TLS entirely
};

struct ServeOpts {
    std::string   listen_addr = "0.0.0.0:9000";
    std::string   root;
    CommonTlsArgs tls;
};

struct SendOpts {
    std::string   remote;
    std::string   source;
    std::string   out;
    CommonTlsArgs tls;
};

[[nodiscard]] bool parse_host_port(const std::string& s,
                                   std::string&       host,
                                   uint16_t&          port) {
    const auto colon = s.rfind(':');
    if (colon == std::string::npos || colon + 1 >= s.size()) return false;
    host                  = s.substr(0, colon);
    const auto port_str   = s.substr(colon + 1);
    try {
        const auto p = std::stoi(port_str);
        if (p < 1 || p > 65535) return false;
        port = static_cast<uint16_t>(p);
    } catch (...) {
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<ftx::transport::TlsConfig>
build_tls_config(const CommonTlsArgs& a, bool is_server) {
    if (a.insecure) return std::nullopt;
    if (a.cert.empty() || a.key.empty()) {
        spdlog::error("TLS is required by default; pass --insecure or provide --tls-cert + --tls-key");
        return std::nullopt;
    }
    ftx::transport::TlsConfig cfg;
    cfg.cert_path   = a.cert;
    cfg.key_path    = a.key;
    cfg.ca_path     = a.ca;
    cfg.verify_peer = !a.no_verify_peer;
    if (!is_server) cfg.sni_host = a.sni;
    return cfg;
}

void add_tls_options(CLI::App* sub, CommonTlsArgs& a) {
    sub->add_option("--tls-cert", a.cert, "PEM certificate (chain) for this side");
    sub->add_option("--tls-key",  a.key,  "PEM private key for --tls-cert");
    sub->add_option("--tls-ca",   a.ca,   "PEM bundle of trusted CAs (peer verification)");
    sub->add_flag  ("--no-verify-peer", a.no_verify_peer,
                    "do not require / verify a peer certificate");
    sub->add_option("--sni", a.sni, "client only — server name to send + verify against");
    sub->add_flag  ("--insecure", a.insecure,
                    "disable TLS entirely (plain TCP — local testing only)");
}

int run_serve(const ServeOpts& o) {
    std::string host;
    uint16_t    port = 0;
    if (!parse_host_port(o.listen_addr, host, port)) {
        spdlog::error("invalid --listen value: {}", o.listen_addr);
        return 2;
    }

    asio::io_context        io;
    asio::ip::tcp::endpoint endpoint;
    asio::error_code        ec;
    if (host == "0.0.0.0") {
        endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
    } else {
        const auto addr = asio::ip::make_address(host, ec);
        if (ec) {
            spdlog::error("invalid host: {} ({})", host, ec.message());
            return 2;
        }
        endpoint = asio::ip::tcp::endpoint(addr, port);
    }

    if (o.tls.insecure) {
        ftx::transport::Server srv(io, endpoint, o.root);
        spdlog::info("ftx serve: PLAIN TCP on {}:{} root={}",
                     endpoint.address().to_string(), srv.local_port(), o.root);
        srv.run();
        return 0;
    }
    auto tls = build_tls_config(o.tls, /*is_server=*/true);
    if (!tls) return 2;
    ftx::transport::Server srv(io, endpoint, o.root, *tls);
    spdlog::info("ftx serve: TLS{} on {}:{} root={}",
                 tls->verify_peer ? "+mTLS" : "",
                 endpoint.address().to_string(), srv.local_port(), o.root);
    srv.run();
    return 0;
}

int run_send(const SendOpts& o) {
    std::string host;
    uint16_t    port = 0;
    if (!parse_host_port(o.remote, host, port)) {
        spdlog::error("invalid remote: {} (expected host:port)", o.remote);
        return 2;
    }

    const std::filesystem::path src(o.source);
    const std::string remote_dest = o.out.empty()
                                        ? src.filename().generic_string()
                                        : o.out;

    asio::io_context           io;
    ftx::transport::ClientOptions opts;
    if (!o.tls.insecure) {
        auto tls = build_tls_config(o.tls, /*is_server=*/false);
        if (!tls) return 2;
        opts.tls = std::move(*tls);
    }
    ftx::transport::Client cli(io, opts);
    if (!cli.send(host, port, src, remote_dest)) {
        spdlog::error("send failed: {}", cli.last_error());
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    CLI::App app{"ftx — secure cross-platform TCP file transfer"};
    app.set_version_flag("--version", std::string{ftx::version()});

    ServeOpts serve_opts;
    auto* serve = app.add_subcommand("serve", "Run as receiver (server)");
    serve->add_option("--listen", serve_opts.listen_addr,
                      "host:port to listen on (default 0.0.0.0:9000)")
        ->capture_default_str();
    serve->add_option("--root", serve_opts.root,
                      "root directory for incoming files")
        ->required();
    add_tls_options(serve, serve_opts.tls);

    SendOpts send_opts;
    auto* send = app.add_subcommand("send", "Send a file to a remote ftx serve");
    send->add_option("remote", send_opts.remote,
                     "host:port of remote ftx serve")->required();
    send->add_option("source", send_opts.source, "local file to send")->required();
    send->add_option("--out", send_opts.out,
                     "remote destination path (default: source filename)");
    add_tls_options(send, send_opts.tls);

    app.require_subcommand(1);
    CLI11_PARSE(app, argc, argv);

    try {
        if (serve->parsed()) return run_serve(serve_opts);
        if (send->parsed())  return run_send(send_opts);
    } catch (const std::exception& e) {
        spdlog::error("fatal: {}", e.what());
        return 3;
    }
    return 0;
}
