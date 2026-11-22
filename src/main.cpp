// WeBjammin — entry point.
//
//   webjammin                      run the game
//   webjammin --selftest           print diagnostics and exit
//   webjammin --generate-songs DIR write the bundled procedural songs
//   webjammin --headless           run without opening /dev/fb0
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.hpp"
#include "app/catalogue.hpp"
#include "util/fs.hpp"

int main(int argc, char** argv) {
    wj::AppOptions options;
    std::string generate_dir;
    std::string render_name;
    std::string render_path;
    std::string catalogue_url;
    std::string catalogue_dest;
    int catalogue_index = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") {
            options.selftest = true;
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--generate-songs") {
            options.generate_songs = true;
            if (i + 1 < argc) generate_dir = argv[++i];
        } else if (arg == "--fetch-catalogue" && i + 1 < argc) {
            catalogue_url = argv[++i];
        } else if (arg == "--download-catalogue" && i + 3 < argc) {
            catalogue_url = argv[++i];
            catalogue_index = std::atoi(argv[++i]);
            catalogue_dest = argv[++i];
        } else if (arg == "--render" && i + 2 < argc) {
            render_name = argv[++i];
            render_path = argv[++i];
        } else if (arg == "--config") {
            if (i + 1 < argc) options.config_path = argv[++i];
        } else if (arg.rfind("--config=", 0) == 0) {
            options.config_path = arg.substr(9);
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "webjammin - rhythm guitar for the M5Stack Cardputer Zero\n\n"
                "  webjammin                       run the game\n"
                "  webjammin --selftest            diagnostics and exit\n"
                "  webjammin --generate-songs DIR  write the bundled songs\n"
                "  webjammin --headless            do not open /dev/fb0\n"
                "  webjammin --config PATH         use an alternative config\n"
                "  webjammin --render NAME OUT.ppm write one screen to a PPM\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }

    if (!catalogue_url.empty()) {
        wj::Catalogue catalogue;
        catalogue.set_index_url(catalogue_url);
        std::string err;
        if (!catalogue.refresh(err)) {
            std::fprintf(stderr, "catalogue: %s\n", err.c_str());
            return 1;
        }
        std::printf("catalogue: %zu song(s)\n", catalogue.entries().size());
        for (const auto& e : catalogue.entries()) {
            std::printf("  %-28s %s\n", e.title.c_str(), e.chart_url.c_str());
        }
        if (!catalogue_dest.empty()) {
            if (catalogue_index < 0 ||
                catalogue_index >= static_cast<int>(catalogue.entries().size())) {
                std::fprintf(stderr, "no such catalogue entry\n");
                return 1;
            }
            const wj::CatalogueEntry& entry =
                catalogue.entries()[static_cast<size_t>(catalogue_index)];
            if (!catalogue.download(entry, catalogue_dest, err,
                                   [](int pct, const std::string& stage) {
                                       std::printf("  %s %d%%\n", stage.c_str(), pct);
                                   })) {
                std::fprintf(stderr, "download failed: %s\n", err.c_str());
                return 1;
            }
            std::printf("downloaded '%s' into %s\n", entry.title.c_str(),
                        catalogue_dest.c_str());
        }
        return 0;
    }

    if (options.generate_songs) {
        if (generate_dir.empty()) generate_dir = "songs";
        std::string err;
        const int rc = wj::App::generate_songs(generate_dir, err);
        if (rc != 0) {
            std::fprintf(stderr, "generate-songs: %s\n", err.c_str());
            return rc;
        }
        return 0;
    }

    wj::App app;
    std::string err;
    if (!app.init(options, err)) {
        std::fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    if (!render_name.empty()) {
        const bool ok = app.render_screen(render_name, render_path);
        app.shutdown();
        return ok ? 0 : 1;
    }
    if (options.selftest) {
        const int rc = app.selftest();
        app.shutdown();
        return rc;
    }
    app.run();
    app.shutdown();
    return 0;
}
