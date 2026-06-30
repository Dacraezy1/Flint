#pragma once

#include <QString>
#include <QVector>
#include <QObject>
#include <functional>

namespace flint::java {

struct JavaInfo {
    QString path;         // Absolute path to the java executable
    QString name;         // Display name
    int majorVersion = 0; // Major version (e.g. 8, 17, 21)
};

class JavaManager : public QObject {
    Q_OBJECT
public:
    explicit JavaManager(QObject* parent = nullptr);

    // Scan standard system paths for Java installations
    QVector<JavaInfo> scan_system_javas();

    // Gets the best Java installation for a specific major version
    QString get_or_download_java(int majorVersion, std::function<void(int pct, const QString& status)> progressCb = nullptr);

    // Downloads and extracts Java of a specific major version (8, 17, 21)
    bool download_java(int majorVersion, std::function<void(int pct, const QString& status)> progressCb = nullptr);

    // Returns path of downloaded Java if it exists
    static QString get_downloaded_java_path(int majorVersion);
};

} // namespace flint::java
