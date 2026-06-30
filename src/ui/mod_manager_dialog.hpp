#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QLabel>
#include "minecraft/instances.hpp"

namespace flint::ui {

struct ModrinthResult {
    QString projectId;
    QString title;
    QString description;
    QString slug;
};

class ModManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModManagerDialog(const minecraft::Instance& inst, QWidget* parent = nullptr);

private slots:
    void on_add_local_mods();
    void on_delete_selected_mod();
    void on_install_preset_clicked();
    void on_search_mods();
    void on_download_selected_mod();
    void on_search_result_selection_changed();

private:
    void refresh_installed_mods();
    void apply_styles();

    minecraft::Instance m_instance;
    QString m_modsDir;

    QTabWidget* m_tabWidget;

    // Installed Tab Widgets
    QListWidget* m_installedList;
    QPushButton* m_addLocalBtn;
    QPushButton* m_deleteModBtn;
    QPushButton* m_installPresetBtn;

    // Search Tab Widgets
    QLineEdit* m_searchBar;
    QPushButton* m_searchBtn;
    QListWidget* m_searchResultsList;
    QLabel* m_modDescriptionLabel;
    QPushButton* m_downloadBtn;

    QVector<ModrinthResult> m_searchResults;
};

} // namespace flint::ui
