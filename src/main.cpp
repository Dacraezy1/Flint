#include <QApplication>
#include "ui/mainwindow.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/logging.hpp"

int main(int argc, char* argv[]) {
    // Enable High DPI scaling
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    app.setApplicationName("Flint");
    app.setApplicationVersion("0.1.0");

    // Initialize flint files directories
    flint::fs::ensure_directories_exist();

    // Initialize structured logging
    std::string logPath = flint::fs::get_cache_dir() + "/flint.log";
    flint::logging::init(logPath);
    flint::logging::info("Starting Flint Minecraft Launcher v0.1.0...");

    flint::ui::MainWindow w;
    w.show();

    return app.exec();
}
