#include "minecraft.hpp"
#include "instances.hpp"
#include "filesystem/filesystem.hpp"
#include "network/network.hpp"
#include "downloads/download_manager.hpp"
#include "logging/logging.hpp"
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <nlohmann/json.hpp>
#include <zip.h>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace flint::minecraft {

namespace {
QString convert_maven_to_path(const QString& name) {
    QStringList parts = name.split(':');
    if (parts.size() < 3) return "";
    
    QString group = parts[0];
    QString artifact = parts[1];
    QString version = parts[2];
    QString classifier = parts.size() > 3 ? parts[3] : "";
    
    QString groupPath = group;
    groupPath.replace('.', '/');
    
    QString fileName;
    if (!classifier.isEmpty()) {
        fileName = QString("%1-%2-%3.jar").arg(artifact).arg(version).arg(classifier);
    } else {
        fileName = QString("%1-%2.jar").arg(artifact).arg(version);
    }
    
    return QString("%1/%2/%3/%4").arg(groupPath).arg(artifact).arg(version).arg(fileName);
}
}

MinecraftManager::MinecraftManager(QObject* parent) : QObject(parent) {}

bool MinecraftManager::fetch_version_manifest() {
    QString manifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
    QString cachePath = QString::fromStdString(flint::fs::get_cache_dir()) + "/version_manifest.json";
    
    QByteArray data;
    bool success = false;
    
    // Try online download first
    flint::logging::info("Fetching Minecraft version manifest...");
    data = flint::network::get(manifestUrl, {}, &success);
    
    if (success) {
        // Write to cache
        QFile file(cachePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }
    } else {
        // Offline-first fallback: load from cache
        flint::logging::warn("Failed to fetch version manifest online. Attempting offline cache...");
        QFile file(cachePath);
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
            success = true;
        } else {
            flint::logging::error("No cached version manifest found.");
            return false;
        }
    }

    try {
        json j = json::parse(data.constData());
        m_versions.clear();
        
        if (j.contains("versions") && j["versions"].is_array()) {
            for (const auto& item : j["versions"]) {
                VersionItem ver;
                ver.id = QString::fromStdString(item.value("id", ""));
                ver.type = QString::fromStdString(item.value("type", ""));
                ver.url = QString::fromStdString(item.value("url", ""));
                ver.releaseTime = QString::fromStdString(item.value("releaseTime", ""));
                m_versions.append(ver);
            }
        }
        
        m_manifestFetched = true;
        emit manifest_fetched();
        return true;
    } catch (const std::exception& e) {
        flint::logging::error("Failed to parse version manifest: {}", e.what());
        return false;
    }
}

QVector<VersionItem> MinecraftManager::get_versions(bool includeSnapshots) const {
    if (includeSnapshots) {
        return m_versions;
    }
    
    QVector<VersionItem> filtered;
    for (const auto& v : m_versions) {
        if (v.type == "release") {
            filtered.append(v);
        }
    }
    return filtered;
}

bool MinecraftManager::evaluate_library_rules(const json& rulesObj) const {
    if (rulesObj.is_null() || !rulesObj.is_array()) {
        return true;
    }
    
    bool allowed = false;
    for (const auto& rule : rulesObj) {
        std::string action = rule.value("action", "allow");
        bool isAllow = (action == "allow");
        
        if (rule.contains("os")) {
            const auto& osObj = rule["os"];
            std::string osName = osObj.value("name", "");
            
            if (osName == "linux") {
                allowed = isAllow;
            }
            
            if (osObj.contains("arch")) {
                std::string arch = osObj.value("arch", "");
                if (arch != "x86" && arch != "x64" && arch != "amd64" && arch != "x86_64") {
                    allowed = !isAllow;
                }
            }
        } else {
            allowed = isAllow;
        }
    }
    return allowed;
}

bool MinecraftManager::extract_zip(const QString& zipPath, const QString& destDir) {
    int err = 0;
    zip* z = zip_open(zipPath.toLocal8Bit().constData(), 0, &err);
    if (!z) {
        flint::logging::error("Failed to open zip file for native extraction: {}", zipPath.toStdString());
        return false;
    }
    
    QDir().mkpath(destDir);
    zip_int64_t num_entries = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        struct zip_stat st;
        if (zip_stat_index(z, i, 0, &st) != 0) {
            continue;
        }
        
        QString name = QString::fromUtf8(st.name);
        
        // Skip metadata, manifest folders or directories
        if (name.endsWith('/') || name.contains("META-INF")) {
            continue;
        }
        
        // Only extract shared objects (.so files) on Linux
        if (!name.endsWith(".so")) {
            continue;
        }
        
        zip_file* f = zip_fopen_index(z, i, 0);
        if (!f) {
            continue;
        }
        
        QString outPath = QDir(destDir).filePath(QFileInfo(name).fileName());
        QFile outFile(outPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            char buf[8192];
            zip_int64_t n;
            while ((n = zip_fread(f, buf, sizeof(buf))) > 0) {
                outFile.write(buf, n);
            }
            outFile.close();
        }
        zip_fclose(f);
    }
    zip_close(z);
    return true;
}

int MinecraftManager::get_required_java_version(const QString& versionId) {
    QString mcDir = QString::fromStdString(flint::fs::get_minecraft_dir());
    QString versionJsonPath = mcDir + QString("/versions/%1/%2.json").arg(versionId).arg(versionId);
    
    if (!QFileInfo::exists(versionJsonPath)) {
        QString detailsUrl;
        for (const auto& v : m_versions) {
            if (v.id == versionId) {
                detailsUrl = v.url;
                break;
            }
        }
        if (detailsUrl.isEmpty()) {
            return 8;
        }
        
        QDir().mkpath(QFileInfo(versionJsonPath).absolutePath());
        if (!flint::network::download_file(detailsUrl, versionJsonPath)) {
            return 8;
        }
    }
    
    std::ifstream file(versionJsonPath.toStdString());
    if (!file.is_open()) {
        return 8;
    }
    
    try {
        json details;
        file >> details;
        if (details.contains("javaVersion") && details["javaVersion"].is_object()) {
            const auto& jv = details["javaVersion"];
            if (jv.contains("majorVersion")) {
                return jv["majorVersion"].get<int>();
            }
        }
    } catch (...) {
        // Fallback
    }
    
    // In older Minecraft versions (like 1.16.5 and older), there is no javaVersion block.
    // Let's implement a fallback heuristic: if minor version is 17 or higher, Java 17. Otherwise Java 8.
    auto parts = versionId.split('.');
    if (parts.size() >= 2) {
        bool ok = false;
        int minor = parts[1].toInt(&ok);
        if (ok) {
            if (minor >= 17) return 17;
        }
    }
    return 8;
}

bool MinecraftManager::prepare_version(const QString& versionId, const QString& instanceName, std::function<void(int pct, const QString& status)> progressCb) {
    // 1. Get version details URL from manifest
    QString detailsUrl;
    for (const auto& v : m_versions) {
        if (v.id == versionId) {
            detailsUrl = v.url;
            break;
        }
    }
    
    if (detailsUrl.isEmpty()) {
        flint::logging::error("Version {} not found in manifest.", versionId.toStdString());
        return false;
    }
    
    QString mcDir = QString::fromStdString(flint::fs::get_minecraft_dir());
    QString versionJsonPath = mcDir + QString("/versions/%1/%2.json").arg(versionId).arg(versionId);
    
    // Download version JSON details if missing
    if (!QFileInfo::exists(versionJsonPath)) {
        if (progressCb) progressCb(5, "Downloading version metadata...");
        bool ok = flint::network::download_file(detailsUrl, versionJsonPath);
        if (!ok) {
            flint::logging::error("Failed to download version details from {}", detailsUrl.toStdString());
            return false;
        }
    }
    
    // Parse version details
    std::ifstream file(versionJsonPath.toStdString());
    if (!file.is_open()) {
        flint::logging::error("Failed to open version JSON details.");
        return false;
    }
    
    json details;
    try {
        file >> details;
    } catch (const std::exception& e) {
        flint::logging::error("JSON parse error in version details: {}", e.what());
        return false;
    }
    
    QVector<downloads::DownloadTask> downloadTasks;
    
    // 2. Client jar task
    if (details.contains("downloads") && details["downloads"].contains("client")) {
        const auto& client = details["downloads"]["client"];
        downloads::DownloadTask task;
        task.url = QString::fromStdString(client.value("url", ""));
        task.path = mcDir + QString("/versions/%1/%2.jar").arg(versionId).arg(versionId);
        task.sha1 = QString::fromStdString(client.value("sha1", ""));
        task.size = client.value("size", 0ll);
        downloadTasks.append(task);
    }
    
    // 3. Libraries tasks
    QVector<QString> nativesToExtract;
    if (details.contains("libraries") && details["libraries"].is_array()) {
        for (const auto& lib : details["libraries"]) {
            // Check OS rules
            if (lib.contains("rules") && !evaluate_library_rules(lib["rules"])) {
                continue;
            }
            
            // Check native classifiers (mostly for Minecraft < 1.19)
            bool isNative = false;
            QString nativeClassifier;
            if (lib.contains("natives") && lib["natives"].is_object()) {
                const auto& natives = lib["natives"];
                if (natives.contains("linux")) {
                    nativeClassifier = QString::fromStdString(natives.value("linux", ""));
                    isNative = true;
                }
            }
            
            if (isNative && !nativeClassifier.isEmpty()) {
                if (lib.contains("downloads") && lib["downloads"].contains("classifiers")) {
                    const auto& classObj = lib["downloads"]["classifiers"];
                    std::string classKey = nativeClassifier.toStdString();
                    if (classObj.contains(classKey)) {
                        const auto& artifact = classObj[classKey];
                        downloads::DownloadTask task;
                        task.url = QString::fromStdString(artifact.value("url", ""));
                        QString relPath = QString::fromStdString(artifact.value("path", ""));
                        task.path = mcDir + "/libraries/" + relPath;
                        task.sha1 = QString::fromStdString(artifact.value("sha1", ""));
                        task.size = artifact.value("size", 0ll);
                        downloadTasks.append(task);
                        
                        nativesToExtract.append(task.path);
                    }
                }
            }
            
            // Check normal artifact
            if (lib.contains("downloads") && lib["downloads"].contains("artifact")) {
                const auto& artifact = lib["downloads"]["artifact"];
                downloads::DownloadTask task;
                task.url = QString::fromStdString(artifact.value("url", ""));
                QString relPath = QString::fromStdString(artifact.value("path", ""));
                task.path = mcDir + "/libraries/" + relPath;
                task.sha1 = QString::fromStdString(artifact.value("sha1", ""));
                task.size = artifact.value("size", 0ll);
                downloadTasks.append(task);
            }
        }
    }
    
    // Add Fabric loader libraries if Fabric is installed
    QString fabricProfilePath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + instanceName + "/fabric_profile.json";
    if (QFileInfo::exists(fabricProfilePath)) {
        std::ifstream fabFile(fabricProfilePath.toStdString());
        if (fabFile.is_open()) {
            try {
                json fabDetails;
                fabFile >> fabDetails;
                if (fabDetails.contains("libraries") && fabDetails["libraries"].is_array()) {
                    for (const auto& lib : fabDetails["libraries"]) {
                        QString libName = QString::fromStdString(lib.value("name", ""));
                        QString libUrl = QString::fromStdString(lib.value("url", "https://libraries.minecraft.net/"));
                        if (libName.isEmpty()) continue;
                        
                        QString relPath = convert_maven_to_path(libName);
                        if (relPath.isEmpty()) continue;
                        
                        downloads::DownloadTask task;
                        task.url = libUrl + relPath;
                        task.path = mcDir + "/libraries/" + relPath;
                        task.sha1 = "";
                        task.size = 0;
                        downloadTasks.append(task);
                    }
                }
            } catch (...) {
                flint::logging::warn("Failed to parse fabric_profile.json during preparation.");
            }
        }
    }
    
    // 4. Asset Index task & assets objects
    if (details.contains("assetIndex") && details["assetIndex"].is_object()) {
        const auto& assetIdx = details["assetIndex"];
        QString assetIdxUrl = QString::fromStdString(assetIdx.value("url", ""));
        QString assetIdxId = QString::fromStdString(assetIdx.value("id", ""));
        QString assetIdxSha1 = QString::fromStdString(assetIdx.value("sha1", ""));
        qint64 assetIdxSize = assetIdx.value("size", 0ll);
        
        QString assetIdxPath = mcDir + QString("/assets/indexes/%1.json").arg(assetIdxId);
        
        // Download index if missing
        if (!QFileInfo::exists(assetIdxPath) || !downloads::DownloadManager::verify_sha1(assetIdxPath, assetIdxSha1)) {
            if (progressCb) progressCb(10, "Downloading asset index...");
            bool ok = flint::network::download_file(assetIdxUrl, assetIdxPath);
            if (!ok) {
                flint::logging::error("Failed to download asset index.");
                return false;
            }
        }
        
        // Parse asset index to add object download tasks
        std::ifstream idxFile(assetIdxPath.toStdString());
        if (idxFile.is_open()) {
            json idxJson;
            try {
                idxFile >> idxJson;
                if (idxJson.contains("objects") && idxJson["objects"].is_object()) {
                    const auto& objects = idxJson["objects"];
                    for (auto it = objects.begin(); it != objects.end(); ++it) {
                        const auto& obj = it.value();
                        std::string hash = obj.value("hash", "");
                        qint64 size = obj.value("size", 0ll);
                        if (hash.size() >= 2) {
                            std::string prefix = hash.substr(0, 2);
                            downloads::DownloadTask task;
                            task.url = QString("https://resources.download.minecraft.net/%1/%2").arg(QString::fromStdString(prefix)).arg(QString::fromStdString(hash));
                            task.path = mcDir + QString("/assets/objects/%1/%2").arg(QString::fromStdString(prefix)).arg(QString::fromStdString(hash));
                            task.sha1 = QString::fromStdString(hash);
                            task.size = size;
                            downloadTasks.append(task);
                        }
                    }
                }
            } catch (const std::exception& e) {
                flint::logging::error("Error parsing asset index: {}", e.what());
            }
        }
    }
    
    // 5. Download everything in parallel
    if (progressCb) progressCb(15, QString("Preparing %1 files...").arg(downloadTasks.size()));
    
    downloads::DownloadManager dl;
    QEventLoop loop;
    connect(&dl, &downloads::DownloadManager::finished, &loop, &QEventLoop::quit);
    connect(&dl, &downloads::DownloadManager::progress, [&](int pct, const QString& status) {
        if (progressCb) {
            // scale progress to 15% - 95% range
            int scaledPct = 15 + static_cast<int>((pct * 80) / 100);
            progressCb(scaledPct, status);
        }
    });
    
    dl.start_downloads(downloadTasks);
    loop.exec();
    
    // 6. Extract natives
    if (progressCb) progressCb(96, "Extracting native libraries...");
    QString nativesDir = mcDir + QString("/versions/%1/natives").arg(versionId);
    QDir(nativesDir).removeRecursively(); // Clean old natives
    
    for (const auto& nativeJar : nativesToExtract) {
        if (QFileInfo::exists(nativeJar)) {
            flint::logging::info("Extracting natives from {}", nativeJar.toStdString());
            extract_zip(nativeJar, nativesDir);
        }
    }
    
    if (progressCb) progressCb(100, "Version preparation finished successfully!");
    return true;
}

QProcess* MinecraftManager::launch_game(const QString& versionId, 
                                        const QString& instanceName, 
                                        const accounts::Account& account,
                                        const QString& javaPath, 
                                        int maxRamMb, 
                                        const QStringList& extraJvmArgs) {
    QString mcDir = QString::fromStdString(flint::fs::get_minecraft_dir());
    QString versionJsonPath = mcDir + QString("/versions/%1/%2.json").arg(versionId).arg(versionId);
    
    std::ifstream file(versionJsonPath.toStdString());
    if (!file.is_open()) {
        flint::logging::error("Failed to read version JSON for launch.");
        return nullptr;
    }
    
    json details;
    try {
        file >> details;
    } catch (...) {
        return nullptr;
    }
    
    // Build Classpath
    QStringList classpathItems;
    if (details.contains("libraries") && details["libraries"].is_array()) {
        for (const auto& lib : details["libraries"]) {
            if (lib.contains("rules") && !evaluate_library_rules(lib["rules"])) {
                continue;
            }
            if (lib.contains("downloads") && lib["downloads"].contains("artifact")) {
                QString relPath = QString::fromStdString(lib["downloads"]["artifact"].value("path", ""));
                classpathItems.append(mcDir + "/libraries/" + relPath);
            }
        }
    }
    // Append client jar
    classpathItems.append(mcDir + QString("/versions/%1/%2.jar").arg(versionId).arg(versionId));
    
    // Check and append Fabric libraries, and determine mainClass
    QString mainClass = "net.minecraft.client.main.Main";
    QString fabricProfilePath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + instanceName + "/fabric_profile.json";
    bool isFabric = QFileInfo::exists(fabricProfilePath);
    if (isFabric) {
        std::ifstream fabFile(fabricProfilePath.toStdString());
        if (fabFile.is_open()) {
            try {
                json fabDetails;
                fabFile >> fabDetails;
                mainClass = QString::fromStdString(fabDetails.value("mainClass", "net.fabricmc.loader.impl.launch.knot.KnotClient"));
                if (fabDetails.contains("libraries") && fabDetails["libraries"].is_array()) {
                    for (const auto& lib : fabDetails["libraries"]) {
                        QString libName = QString::fromStdString(lib.value("name", ""));
                        if (libName.isEmpty()) continue;
                        QString relPath = convert_maven_to_path(libName);
                        if (!relPath.isEmpty()) {
                            classpathItems.append(mcDir + "/libraries/" + relPath);
                        }
                    }
                }
            } catch (...) {
                flint::logging::warn("Failed to parse fabric_profile.json during launch.");
            }
        }
    } else {
        mainClass = QString::fromStdString(details.value("mainClass", "net.minecraft.client.main.Main"));
    }
    
    QString classpath = classpathItems.join(':');
    
    // Extract assets index details
    QString assetsIdxId = "legacy";
    if (details.contains("assetIndex") && details["assetIndex"].is_object()) {
        assetsIdxId = QString::fromStdString(details["assetIndex"].value("id", "legacy"));
    }
    
    QString nativesDir = mcDir + QString("/versions/%1/natives").arg(versionId);
    QString gameDir = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + instanceName;
    QDir().mkpath(gameDir);
    
    // Map placeholders
    auto replace_variables = [&](QString arg) -> QString {
        arg.replace("${auth_player_name}", account.username);
        arg.replace("${auth_uuid}", account.uuid);
        arg.replace("${auth_access_token}", account.accessToken);
        arg.replace("${user_type}", "mojang");
        arg.replace("${version_name}", versionId);
        arg.replace("${game_directory}", gameDir);
        arg.replace("${assets_root}", mcDir + "/assets");
        arg.replace("${assets_index_name}", assetsIdxId);
        arg.replace("${version_type}", "Flint");
        arg.replace("${natives_directory}", nativesDir);
        arg.replace("${classpath}", classpath);
        arg.replace("${launcher_name}", "Flint");
        arg.replace("${launcher_version}", "0.1.0");
        arg.replace("${auth_active_profile_properties}", "{}");
        return arg;
    };
    
    QStringList jvmArgs;
    QStringList gameArgs;
    
    // Check modern arguments node
    if (details.contains("arguments") && details["arguments"].is_object()) {
        const auto& argsNode = details["arguments"];
        
        // JVM arguments
        if (argsNode.contains("jvm") && argsNode["jvm"].is_array()) {
            for (const auto& item : argsNode["jvm"]) {
                if (item.is_string()) {
                    jvmArgs.append(replace_variables(QString::fromStdString(item.get<std::string>())));
                } else if (item.is_object()) {
                    if (item.contains("rules") && evaluate_library_rules(item["rules"])) {
                        if (item.contains("value")) {
                            const auto& val = item["value"];
                            if (val.is_string()) {
                                jvmArgs.append(replace_variables(QString::fromStdString(val.get<std::string>())));
                            } else if (val.is_array()) {
                                for (const auto& v : val) {
                                    jvmArgs.append(replace_variables(QString::fromStdString(v.get<std::string>())));
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Game arguments
        if (argsNode.contains("game") && argsNode["game"].is_array()) {
            for (const auto& item : argsNode["game"]) {
                if (item.is_string()) {
                    gameArgs.append(replace_variables(QString::fromStdString(item.get<std::string>())));
                } else if (item.is_object()) {
                    if (item.contains("rules") && evaluate_library_rules(item["rules"])) {
                        if (item.contains("value")) {
                            const auto& val = item["value"];
                            if (val.is_string()) {
                                gameArgs.append(replace_variables(QString::fromStdString(val.get<std::string>())));
                            } else if (val.is_array()) {
                                for (const auto& v : val) {
                                    gameArgs.append(replace_variables(QString::fromStdString(v.get<std::string>())));
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Legacy minecraftArguments style (<= 1.12)
        QString legacyStr = QString::fromStdString(details.value("minecraftArguments", ""));
        auto parts = legacyStr.split(' ');
        for (const auto& part : parts) {
            gameArgs.append(replace_variables(part));
        }
        
        // Default JVM arguments for legacy
        jvmArgs << QString("-Djava.library.path=%1").arg(nativesDir);
        jvmArgs << "-cp" << classpath;
    }
    
    // Add memory limits & extras
    jvmArgs.prepend(QString("-Xmx%1M").arg(maxRamMb));
    jvmArgs.prepend("-Xms512M");
    for (const auto& x : extraJvmArgs) {
        jvmArgs.append(x);
    }
    
    // Main Class (resolved above)
    
    // Assemble complete arguments list
    QStringList fullArgs;
    fullArgs << jvmArgs << mainClass << gameArgs;
    
    Instance inst;
    bool hasConfig = InstanceManager::load_instance_config(instanceName, inst);
    bool useWayland = hasConfig ? inst.useWayland : true;
    bool useMangoHud = hasConfig ? inst.useMangoHud : false;
    bool useGameMode = hasConfig ? inst.useGameMode : false;

    QString program = javaPath;
    QStringList args = fullArgs;
    
    // Wrap command line based on performance toggles
    if (useGameMode && useMangoHud) {
        program = "gamemoderun";
        args.prepend(javaPath);
        args.prepend("mangohud");
    } else if (useGameMode) {
        program = "gamemoderun";
        args.prepend(javaPath);
    } else if (useMangoHud) {
        program = "mangohud";
        args.prepend(javaPath);
    }
    
    flint::logging::info("Launching Minecraft with executable: {}", program.toStdString());
    flint::logging::info("Arguments: {}", args.join(' ').toStdString());
    
    auto* process = new QProcess();
    process->setWorkingDirectory(gameDir);
    process->setProgram(program);
    process->setArguments(args);
    
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (useWayland) {
        env.insert("GLFW_PLATFORM", "wayland");
    }
    process->setProcessEnvironment(env);
    
    process->start();
    return process;
}

bool MinecraftManager::install_fabric(const QString& mcVersion, const QString& instanceName) {
    flint::logging::info("Installing Fabric loader for version {} in instance {}", mcVersion.toStdString(), instanceName.toStdString());
    
    // 1. Fetch Fabric loader version list
    QString metaUrl = QString("https://meta.fabricmc.net/v2/versions/loader/%1").arg(mcVersion);
    bool ok = false;
    QByteArray metaData = flint::network::get(metaUrl, {}, &ok);
    if (!ok || metaData.isEmpty()) {
        flint::logging::error("Failed to fetch Fabric loader metadata for version {}", mcVersion.toStdString());
        return false;
    }
    
    QString loaderVersion;
    try {
        json j = json::parse(metaData.constData());
        if (j.is_array() && !j.empty()) {
            // Find the first stable loader
            for (const auto& item : j) {
                if (item.contains("loader") && item["loader"].is_object()) {
                    const auto& l = item["loader"];
                    if (l.value("stable", false)) {
                        loaderVersion = QString::fromStdString(l.value("version", ""));
                        break;
                    }
                }
            }
            // Fallback to the first loader if no stable one is flagged
            if (loaderVersion.isEmpty()) {
                loaderVersion = QString::fromStdString(j[0]["loader"].value("version", ""));
            }
        }
    } catch (...) {
        flint::logging::error("Failed to parse Fabric loader metadata JSON");
        return false;
    }
    
    if (loaderVersion.isEmpty()) {
        flint::logging::error("No Fabric loader version found for Minecraft {}", mcVersion.toStdString());
        return false;
    }
    
    // 2. Fetch the profile JSON from Fabric
    QString profileUrl = QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json")
                         .arg(mcVersion).arg(loaderVersion);
    QByteArray profileData = flint::network::get(profileUrl, {}, &ok);
    if (!ok || profileData.isEmpty()) {
        flint::logging::error("Failed to fetch Fabric profile JSON from {}", profileUrl.toStdString());
        return false;
    }
    
    // 3. Write it to <instance>/fabric_profile.json
    QString fabricProfilePath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + instanceName + "/fabric_profile.json";
    QFile file(fabricProfilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        flint::logging::error("Failed to open {} for writing Fabric profile", fabricProfilePath.toStdString());
        return false;
    }
    file.write(profileData);
    file.close();
    
    flint::logging::info("Successfully installed Fabric loader version {} for instance {}", loaderVersion.toStdString(), instanceName.toStdString());
    return true;
}

} // namespace flint::minecraft
