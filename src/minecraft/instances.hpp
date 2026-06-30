#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QObject>

namespace flint::minecraft {

struct Instance {
    QString name;
    QString mcVersion;
    QString loader = "vanilla";
    QString javaPath;
    int maxMemory = 2048;
    QStringList jvmFlags;
    QString icon = "default";
    QString notes;
    
    // Linux Integration Options
    bool useWayland = true;
    bool useMangoHud = false;
    bool useGameMode = false;
};

class InstanceManager : public QObject {
    Q_OBJECT
public:
    explicit InstanceManager(QObject* parent = nullptr);

    // Get list of all installed instances
    QVector<Instance> get_instances();

    // Create a new instance
    bool create_instance(const Instance& inst);

    // Edit an existing instance
    bool save_instance(const Instance& inst);

    // Delete an instance and all its files
    bool delete_instance(const QString& name);

    // Clone an instance
    bool clone_instance(const QString& sourceName, const QString& destName);

    // Export instance to a .zip archive
    bool export_instance(const QString& name, const QString& zipPath);

    // Import instance from a .zip archive
    bool import_instance(const QString& zipPath, const QString& destName);

    // Helper to load a single instance config
    static bool load_instance_config(const QString& name, Instance& inst);

signals:
    void instances_changed();
};

} // namespace flint::minecraft
