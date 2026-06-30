#include "java_manager.hpp"
#include "filesystem/filesystem.hpp"
#include "network/network.hpp"
#include "logging/logging.hpp"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <filesystem>
#include <algorithm>

namespace flint::java {

namespace {
int get_java_version_from_binary(const QString& javaPath) {
    QProcess process;
    process.start(javaPath, QStringList() << "-version");
    if (!process.waitForFinished(1000)) {
        return 0;
    }
    
    // java -version writes to stderr
    QString output = QString::fromUtf8(process.readAllStandardError());
    if (output.isEmpty()) {
        output = QString::fromUtf8(process.readAllStandardOutput());
    }
    
    int index = output.indexOf("version \"");
    if (index != -1) {
        QString verStr = output.mid(index + 9);
        int endQuote = verStr.indexOf("\"");
        if (endQuote != -1) {
            verStr = verStr.left(endQuote);
            if (verStr.startsWith("1.")) {
                auto parts = verStr.split('.');
                if (parts.size() >= 2) {
                    return parts[1].toInt();
                }
            } else {
                auto parts = verStr.split('.');
                if (!parts.isEmpty()) {
                    auto subparts = parts[0].split('-');
                    return subparts[0].toInt();
                }
            }
        }
    }
    return 0;
}
} // namespace

JavaManager::JavaManager(QObject* parent) : QObject(parent) {}

QVector<JavaInfo> JavaManager::scan_system_javas() {
    QVector<JavaInfo> results;
    QVector<QString> searchPaths = {
        "/usr/lib/jvm",
        "/usr/lib64/jvm",
        "/usr/lib/sdk"
    };

    // 1. Scan default PATH java
    QString pathJava = QStandardPaths::findExecutable("java");
    if (!pathJava.isEmpty()) {
        int ver = get_java_version_from_binary(pathJava);
        if (ver > 0) {
            JavaInfo info;
            info.path = pathJava;
            info.majorVersion = ver;
            info.name = QString("System Default (Java %1)").arg(ver);
            results.append(info);
        }
    }

    // 2. Scan directories
    for (const auto& searchPath : searchPaths) {
        QDir dir(searchPath);
        if (!dir.exists()) continue;

        for (const auto& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString fullPath = dir.absoluteFilePath(entry);
            // Check potential java executable paths
            QStringList possibleExecutables = {
                fullPath + "/bin/java",
                fullPath + "/jre/bin/java"
            };

            for (const auto& javaPath : possibleExecutables) {
                if (QFileInfo::exists(javaPath)) {
                    int ver = get_java_version_from_binary(javaPath);
                    if (ver > 0) {
                        // Check if we already have this exact path
                        bool duplicate = std::any_of(results.begin(), results.end(), [&](const JavaInfo& x) {
                            return x.path == javaPath;
                        });
                        
                        if (!duplicate) {
                            JavaInfo info;
                            info.path = javaPath;
                            info.majorVersion = ver;
                            info.name = QString("%1 (Java %2)").arg(entry).arg(ver);
                            results.append(info);
                        }
                    }
                }
            }
        }
    }

    return results;
}


QString JavaManager::get_downloaded_java_path(int majorVersion) {
    QString exePath = QString::fromStdString(flint::fs::get_data_dir()) + 
                     QString("/java/jdk%1/bin/java").arg(majorVersion);
    if (QFileInfo::exists(exePath)) {
        return exePath;
    }
    return "";
}

bool JavaManager::download_java(int majorVersion, std::function<void(int pct, const QString& status)> progressCb) {
    if (progressCb) progressCb(5, "Resolving Adoptium URL...");
    
    // Adoptium Temurin API url
    QString url = QString("https://api.adoptium.net/v3/binary/latest/%1/ga/linux/x64/jdk/hotspot/normal/eclipse")
                  .arg(majorVersion);
    
    QString cacheDir = QString::fromStdString(flint::fs::get_cache_dir());
    QString archivePath = cacheDir + QString("/jdk%1.tar.gz").arg(majorVersion);
    
    if (progressCb) progressCb(10, QString("Downloading Java %1...").arg(majorVersion));
    
    flint::logging::info("Downloading Java {} from Adoptium...", majorVersion);
    bool ok = flint::network::download_file(url, archivePath, [&](qint64 rec, qint64 total) {
        if (progressCb && total > 0) {
            int pct = 10 + static_cast<int>((rec * 80) / total);
            progressCb(pct, QString("Downloading Java %1 (%2%)").arg(majorVersion).arg(pct));
        }
    });
    
    if (!ok) {
        flint::logging::error("Failed to download Java {}", majorVersion);
        return false;
    }
    
    if (progressCb) progressCb(90, "Extracting JDK archive...");
    
    QString destDir = QString::fromStdString(flint::fs::get_data_dir()) + QString("/java/jdk%1").arg(majorVersion);
    QDir().mkpath(destDir);
    
    // Extract using tar
    QProcess tarProcess;
    tarProcess.start("tar", QStringList() << "-xzf" << archivePath << "-C" << destDir << "--strip-components=1");
    if (!tarProcess.waitForFinished(15000)) {
        flint::logging::error("Extraction of Java {} timed out or failed.", majorVersion);
        QDir(destDir).removeRecursively();
        return false;
    }
    
    // Remove temporary archive
    QFile::remove(archivePath);
    
    QString javaExe = destDir + "/bin/java";
    if (!QFileInfo::exists(javaExe)) {
        flint::logging::error("Java executable not found after extraction: {}", javaExe.toStdString());
        return false;
    }
    
    flint::logging::info("Java {} successfully installed to {}", majorVersion, destDir.toStdString());
    if (progressCb) progressCb(100, "Java installation complete!");
    return true;
}

QString JavaManager::get_or_download_java(int majorVersion, std::function<void(int pct, const QString& status)> progressCb) {
    // 1. Check if we already have it downloaded
    QString downloadedPath = get_downloaded_java_path(majorVersion);
    if (!downloadedPath.isEmpty()) {
        return downloadedPath;
    }
    
    // 2. Scan system for required version
    auto systemJavas = scan_system_javas();
    for (const auto& java : systemJavas) {
        if (java.majorVersion == majorVersion) {
            return java.path;
        }
    }
    
    // 3. Fallback: download it
    if (download_java(majorVersion, progressCb)) {
        return get_downloaded_java_path(majorVersion);
    }
    
    // 4. Ultimate fallback: try default java
    QString defaultJava = QStandardPaths::findExecutable("java");
    if (!defaultJava.isEmpty()) {
        flint::logging::warn("Using system default java as ultimate fallback, version mismatch possible.");
        return defaultJava;
    }
    
    return "";
}

} // namespace flint::java
