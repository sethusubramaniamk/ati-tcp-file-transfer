#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

#include "ftx/version.hpp"

int main(int argc, char** argv) {
    CLI::App app{"ftx — secure cross-platform TCP file transfer"};
    app.set_version_flag("--version", std::string{ftx::version()});

    auto* serve = app.add_subcommand("serve", "Run as receiver (server)");
    auto* send  = app.add_subcommand("send",  "Send a file to a remote ftx serve");

    // Placeholders — wired up in later phases.
    std::string listen_addr = "0.0.0.0:9000";
    std::string root_dir;
    serve->add_option("--listen", listen_addr, "host:port to listen on")->capture_default_str();
    serve->add_option("--root",   root_dir,    "root directory for incoming files")->required();

    std::string remote;
    std::string source_path;
    std::string dest_path;
    send->add_option("remote",    remote,      "host:port of remote ftx serve")->required();
    send->add_option("source",    source_path, "local file to send")->required();
    send->add_option("--out",     dest_path,   "remote destination path (relative to --root)");

    app.require_subcommand(1);
    CLI11_PARSE(app, argc, argv);

    if (serve->parsed()) {
        std::cout << "[serve] listen=" << listen_addr
                  << " root="           << root_dir
                  << " — not yet implemented (phase 2+)\n";
        return 0;
    }
    if (send->parsed()) {
        std::cout << "[send] remote="   << remote
                  << " source="         << source_path
                  << " out="            << dest_path
                  << " — not yet implemented (phase 2+)\n";
        return 0;
    }
    return 0;
}
