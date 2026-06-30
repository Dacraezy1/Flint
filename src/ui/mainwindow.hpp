#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QTextEdit>
#include <QSlider>
#include <QLabel>
#include <QProcess>
#include <QCheckBox>
#include "accounts/accounts.hpp"
#include "minecraft/minecraft.hpp"
#include "minecraft/instances.hpp"
#include "java/java_manager.hpp"

namespace flint::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void on_add_account_clicked();
    void on_remove_account_clicked();
    void on_ms_login_clicked();
    void on_account_selected(int index);
    void on_play_clicked();
    void on_theme_toggle_clicked();
    
    // Instance management slots
    void on_instance_selected(int index);
    void on_create_instance_clicked();
    void on_delete_instance_clicked();
    void on_clone_instance_clicked();
    void on_export_instance_clicked();
    void on_import_instance_clicked();
    void on_save_instance_settings_clicked();
    void on_manage_mods_clicked();
    
    // Background manifest loading callbacks
    void on_manifest_loaded(bool success);
    
    // Process monitoring slots
    void on_game_started();
    void on_game_finished(int exitCode, QProcess::ExitStatus exitStatus);
    void on_game_stdout();
    void on_game_stderr();

private:
    void setup_ui();
    void apply_theme(bool dark);
    void refresh_accounts_ui();
    void refresh_instances_ui();
    void load_selected_instance_details(const QString& name);
    
    // Core managers
    accounts::AccountManager m_accountManager;
    minecraft::MinecraftManager m_minecraftManager;
    java::JavaManager m_javaManager;
    minecraft::InstanceManager m_instanceManager;

    // Running game process
    QProcess* m_gameProcess = nullptr;

    // UI Widgets
    QWidget* m_centralWidget;
    QListWidget* m_accountsList;
    QLineEdit* m_usernameInput;
    QPushButton* m_addAccountBtn;
    QPushButton* m_removeAccountBtn;
    QPushButton* m_msLoginBtn;
    
    // Instances Widgets
    QListWidget* m_instancesList;
    QPushButton* m_createInstanceBtn;
    QPushButton* m_deleteInstanceBtn;
    QPushButton* m_cloneInstanceBtn;
    QPushButton* m_exportInstanceBtn;
    QPushButton* m_importInstanceBtn;
    
    // Selected Instance Settings Form
    QLineEdit* m_instNameField;
    QComboBox* m_versionCombo;
    QComboBox* m_loaderCombo;
    QSlider* m_ramSlider;
    QLabel* m_ramValueLabel;
    QLineEdit* m_instNotesField;
    QCheckBox* m_waylandCheck;
    QCheckBox* m_mangohudCheck;
    QCheckBox* m_gamemodeCheck;
    QPushButton* m_saveSettingsBtn;
    QPushButton* m_manageModsBtn;
    
    QPushButton* m_playBtn;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    
    QTextEdit* m_consoleLog;
    QPushButton* m_themeBtn;

    bool m_isDark = true;
};

} // namespace flint::ui
