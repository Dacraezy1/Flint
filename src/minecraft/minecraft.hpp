#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QProcess>
#include <functional>
#include <nlohmann/json.hpp>
#include "accounts/accounts.hpp"

namespace flint::minecraft {

struct VersionItem {
    QString id;
    QString type;
    QString url;
    QString releaseTime;
};

class MinecraftManager : public QObject {
    Q_OBJECT
public:
    explicit MinecraftManager(QObject* parent = nullptr);

    // Fetch the Mojang version manifest (returns true on success)
    bool fetch_version_manifest();

    // Get list of fetched versions
    QVector<VersionItem> get_versions(bool includeSnapshots = false) const;

    // Prepare all assets, libraries, client jar, and natives for a specific version & instance
    bool prepare_version(const QString& versionId, const QString& instanceName, std::function<void(int pct, const QString& status)> progressCb = nullptr);

    // Installs Fabric loader profile for the given version and instance
    bool install_fabric(const QString& mcVersion, const QString& instanceName);

    // Resolves the required Java major version from the version details JSON (downloads if missing)
    int get_required_java_version(const QString& versionId);

    // Launches Minecraft and returns the running process
    QProcess* launch_game(const QString& versionId, 
                          const QString& instanceName, 
                          const accounts::Account& account,
                          const QString& javaPath, 
                          int maxRamMb = 2048, 
                          const QStringList& extraJvmArgs = {});

    // Utility to extract zip files (used for libraries natives)
    static bool extract_zip(const QString& zipPath, const QString& destDir);

signals:
    void manifest_fetched();

private:
    QVector<VersionItem> m_versions;
    bool m_manifestFetched = false;

    // Internal helper to evaluate OS rules for libraries
    bool evaluate_library_rules(const nlohmann::json& rulesObj) const;
};

} // namespace flint::minecraft
