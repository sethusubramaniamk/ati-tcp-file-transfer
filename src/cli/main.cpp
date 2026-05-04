#include <CLI/CLI.hpp>
#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "ftx/transport/client.hpp"
#include "ftx/transport/server.hpp"
#include "ftx/version.hpp"

namespace {

struct ServeOpts {
    std::string listen_addr = "0.0.0.0:9000";
    std::string root;
};

struct SendOpts {
    std::string remote;
    std::string source;
    std::string out;
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

int run_serve(const ServeOpts& o) {
    std::string host;
    uint16_t    port = 0;
    if (!parse_host_port(o.listen_addr, host, port)) {
        spdlog::error("invalid --listen value: {}", o.listen_addr);
        return 2;
    }

    asio::io_context io;
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

    ftx::transport::Server srv(io, endpoint, o.root);
    spdlog::info("ftx serve: listening on {} → {}", endpoint.address().to_string(),
                 srv.local_port());
    spdlog::info("           root={}", o.root);
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

    asio::io_context io;
    ftx::transport::Client cli(io);
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

    SendOpts send_opts;
    auto* send = app.add_subcommand("send", "Send a file to a remote ftx serve");
    send->add_option("remote", send_opts.remote,
                     "host:port of remote ftx serve")->required();
    send->add_option("source", send_opts.source, "local file to send")->required();
    send->add_option("--out", send_opts.out,
                     "remote destination path (default: source filename)");

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
