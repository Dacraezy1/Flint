#include "instances.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/logging.hpp"
#include <QDir>
#include <QFileInfo>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <zip.h>

using json = nlohmann::json;

namespace flint::minecraft {

InstanceManager::InstanceManager(QObject* parent) : QObject(parent) {}

bool InstanceManager::load_instance_config(const QString& name, Instance& inst) {
    QString configPath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + name + "/instance.json";
    std::ifstream file(configPath.toStdString());
    if (!file.is_open()) return false;
    
    try {
        json j;
        file >> j;
        inst.name = name;
        inst.mcVersion = QString::fromStdString(j.value("mcVersion", ""));
        inst.loader = QString::fromStdString(j.value("loader", "vanilla"));
        inst.javaPath = QString::fromStdString(j.value("javaPath", ""));
        inst.maxMemory = j.value("maxMemory", 2048);
        inst.icon = QString::fromStdString(j.value("icon", "default"));
        inst.notes = QString::fromStdString(j.value("notes", ""));
        inst.useWayland = j.value("useWayland", true);
        inst.useMangoHud = j.value("useMangoHud", false);
        inst.useGameMode = j.value("useGameMode", false);
        
        inst.jvmFlags.clear();
        if (j.contains("jvmFlags") && j["jvmFlags"].is_array()) {
            for (const auto& flag : j["jvmFlags"]) {
                inst.jvmFlags.append(QString::fromStdString(flag));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

QVector<Instance> InstanceManager::get_instances() {
    QVector<Instance> list;
    QDir dir(QString::fromStdString(flint::fs::get_instances_dir()));
    if (!dir.exists()) return list;
    
    for (const auto& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        Instance inst;
        if (load_instance_config(entry, inst)) {
            list.append(inst);
        } else {
            // Default fallback if json was corrupted
            inst.name = entry;
            inst.mcVersion = "1.20.4"; // standard default fallback
            list.append(inst);
        }
    }
    return list;
}

bool InstanceManager::create_instance(const Instance& inst) {
    QString instDir = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + inst.name;
    QDir().mkpath(instDir);
    return save_instance(inst);
}

bool InstanceManager::save_instance(const Instance& inst) {
    QString configPath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + inst.name + "/instance.json";
    json j;
    j["mcVersion"] = inst.mcVersion.toStdString();
    j["loader"] = inst.loader.toStdString();
    j["javaPath"] = inst.javaPath.toStdString();
    j["maxMemory"] = inst.maxMemory;
    j["icon"] = inst.icon.toStdString();
    j["notes"] = inst.notes.toStdString();
    j["useWayland"] = inst.useWayland;
    j["useMangoHud"] = inst.useMangoHud;
    j["useGameMode"] = inst.useGameMode;
    
    j["jvmFlags"] = json::array();
    for (const auto& flag : inst.jvmFlags) {
        j["jvmFlags"].push_back(flag.toStdString());
    }
    
    try {
        std::ofstream file(configPath.toStdString());
        if (!file.is_open()) return false;
        file << j.dump(4);
        emit instances_changed();
        return true;
    } catch (...) {
        return false;
    }
}

bool InstanceManager::delete_instance(const QString& name) {
    QString instDir = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + name;
    QDir dir(instDir);
    if (dir.exists()) {
        bool ok = dir.removeRecursively();
        if (ok) emit instances_changed();
        return ok;
    }
    return false;
}

bool InstanceManager::clone_instance(const QString& sourceName, const QString& destName) {
    std::filesystem::path src = flint::fs::get_instances_dir() + "/" + sourceName.toStdString();
    std::filesystem::path dst = flint::fs::get_instances_dir() + "/" + destName.toStdString();
    
    try {
        std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive);
        
        // Update name in config of cloned instance
        Instance inst;
        if (load_instance_config(destName, inst)) {
            inst.name = destName;
            save_instance(inst);
        }
        emit instances_changed();
        return true;
    } catch (const std::exception& e) {
        flint::logging::error("Failed to clone instance: {}", e.what());
        return false;
    }
}

bool InstanceManager::export_instance(const QString& name, const QString& zipPath) {
    std::filesystem::path instPath = flint::fs::get_instances_dir() + "/" + name.toStdString();
    if (!std::filesystem::exists(instPath)) return false;
    
    int err = 0;
    zip* z = zip_open(zipPath.toLocal8Bit().constData(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!z) {
        flint::logging::error("Failed to open zip for export: {}", zipPath.toStdString());
        return false;
    }
    
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(instPath)) {
            if (entry.is_regular_file()) {
                std::filesystem::path rel = std::filesystem::relative(entry.path(), instPath);
                
                zip_source* s = zip_source_file(z, entry.path().string().c_str(), 0, 0);
                if (s) {
                    zip_file_add(z, rel.string().c_str(), s, ZIP_FL_OVERWRITE);
                }
            }
        }
        zip_close(z);
        return true;
    } catch (const std::exception& e) {
        flint::logging::error("Exception exporting instance to zip: {}", e.what());
        zip_close(z);
        return false;
    }
}

bool InstanceManager::import_instance(const QString& zipPath, const QString& destName) {
    QString instDir = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + destName;
    QDir().mkpath(instDir);
    
    int err = 0;
    zip* z = zip_open(zipPath.toLocal8Bit().constData(), 0, &err);
    if (!z) {
        flint::logging::error("Failed to open zip for import: {}", zipPath.toStdString());
        return false;
    }
    
    zip_int64_t num_entries = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        struct zip_stat st;
        if (zip_stat_index(z, i, 0, &st) != 0) continue;
        
        QString name = QString::fromUtf8(st.name);
        if (name.endsWith('/')) {
            QDir(instDir).mkpath(name);
            continue;
        }
        
        zip_file* f = zip_fopen_index(z, i, 0);
        if (!f) continue;
        
        QString outPath = QDir(instDir).filePath(name);
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        
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
    
    // Ensure the instance name matches destName in metadata
    Instance inst;
    if (load_instance_config(destName, inst)) {
        inst.name = destName;
        save_instance(inst);
    } else {
        // Create standard default instance.json if missing
        inst.name = destName;
        inst.mcVersion = "1.20.4";
        save_instance(inst);
    }
    
    emit instances_changed();
    return true;
}

} // namespace flint::minecraft
