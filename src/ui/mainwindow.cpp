#include "mainwindow.hpp"
#include "microsoft_login_dialog.hpp"
#include "mod_manager_dialog.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/logging.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QGridLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QProcessEnvironment>
#include <QtConcurrent/QtConcurrent>
#include <QScrollBar>

namespace flint::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Flint Minecraft Launcher");
    resize(950, 620); // Expanded size to neatly accommodate three panels and Linux options
    
    setup_ui();
    apply_theme(m_isDark);
    
    // Ensure filesystem exists
    flint::fs::ensure_directories_exist();
    
    // Load existing accounts
    m_accountManager.load_accounts();
    refresh_accounts_ui();
    
    // Check and create default instance if none exist
    auto insts = m_instanceManager.get_instances();
    if (insts.isEmpty()) {
        minecraft::Instance defaultInst;
        defaultInst.name = "Default";
        defaultInst.mcVersion = "1.20.4"; // Standard stable default
        m_instanceManager.create_instance(defaultInst);
    }
    
    refresh_instances_ui();
    if (m_instancesList->count() > 0) {
        m_instancesList->setCurrentRow(0);
    }
    
    // System diagnostics: Vulkan driver check (standard on Linux Mesa / Nvidia)
    QDir vulkanIcdDir("/usr/share/vulkan/icd.d");
    QDir vulkanIcdUserDir("/etc/vulkan/icd.d");
    bool hasVulkan = (vulkanIcdDir.exists() && !vulkanIcdDir.entryList(QStringList() << "*.json", QDir::Files).isEmpty()) ||
                     (vulkanIcdUserDir.exists() && !vulkanIcdUserDir.entryList(QStringList() << "*.json", QDir::Files).isEmpty());
    
    if (!hasVulkan) {
        flint::logging::warn("No Vulkan drivers detected inside /usr/share/vulkan/icd.d or /etc/vulkan/icd.d.");
        m_consoleLog->append("------------ GRAPHICS DIAGNOSTIC WARNING ------------");
        m_consoleLog->append("No Vulkan driver ICD manifests were detected in standard locations.");
        m_consoleLog->append("If you launch modern Minecraft versions with performance wrappers like Sodium,");
        m_consoleLog->append("the game might fail to start or fall back to software rendering.");
        m_consoleLog->append("Please ensure vulkan drivers (mesa-vulkan-drivers, nvidia-utils, etc.) are installed.");
        m_consoleLog->append("-----------------------------------------------------\n");
    } else {
        flint::logging::info("Vulkan driver ICD manifest files detected. Graphics stack OK.");
    }
    
    // Fetch version manifest asynchronously
    m_statusLabel->setText("Fetching version manifest...");
    m_playBtn->setEnabled(false);
    
    connect(&m_minecraftManager, &minecraft::MinecraftManager::manifest_fetched, this, [this]() {
        on_manifest_loaded(true);
    });
    
    QtConcurrent::run([this]() {
        bool ok = m_minecraftManager.fetch_version_manifest();
        if (!ok) {
            QMetaObject::invokeMethod(this, [this]() {
                on_manifest_loaded(false);
            });
        }
    });
}

MainWindow::~MainWindow() {
    if (m_gameProcess && m_gameProcess->state() == QProcess::Running) {
        m_gameProcess->terminate();
        m_gameProcess->waitForFinished(3000);
    }
}

void MainWindow::setup_ui() {
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    auto* mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);
    
    // --------------------------------------------------
    // PANEL 1: Accounts Management (Left)
    // --------------------------------------------------
    auto* leftPanel = new QVBoxLayout();
    leftPanel->setSpacing(8);
    
    auto* accHeader = new QLabel("ACCOUNTS", this);
    accHeader->setStyleSheet("font-weight: bold; font-size: 11px; letter-spacing: 1px; color: #89b4fa;");
    leftPanel->addWidget(accHeader);
    
    m_accountsList = new QListWidget(this);
    m_accountsList->setFixedWidth(180);
    leftPanel->addWidget(m_accountsList);
    
    m_usernameInput = new QLineEdit(this);
    m_usernameInput->setPlaceholderText("Offline Username");
    m_usernameInput->setFixedWidth(180);
    leftPanel->addWidget(m_usernameInput);
    
    auto* accBtns = new QHBoxLayout();
    m_addAccountBtn = new QPushButton("Add", this);
    m_removeAccountBtn = new QPushButton("Remove", this);
    accBtns->addWidget(m_addAccountBtn);
    accBtns->addWidget(m_removeAccountBtn);
    leftPanel->addLayout(accBtns);
    
    m_msLoginBtn = new QPushButton("Microsoft Login", this);
    m_msLoginBtn->setFixedWidth(180);
    leftPanel->addWidget(m_msLoginBtn);
    
    mainLayout->addLayout(leftPanel);
    
    // Separator 1
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep1);
    
    // --------------------------------------------------
    // PANEL 2: Instances Manager (Middle)
    // --------------------------------------------------
    auto* middlePanel = new QVBoxLayout();
    middlePanel->setSpacing(8);
    
    auto* instHeader = new QLabel("INSTANCES", this);
    instHeader->setStyleSheet("font-weight: bold; font-size: 11px; letter-spacing: 1px; color: #89b4fa;");
    middlePanel->addWidget(instHeader);
    
    m_instancesList = new QListWidget(this);
    m_instancesList->setFixedWidth(200);
    middlePanel->addWidget(m_instancesList);
    
    auto* instBtns1 = new QHBoxLayout();
    m_createInstanceBtn = new QPushButton("New", this);
    m_deleteInstanceBtn = new QPushButton("Delete", this);
    instBtns1->addWidget(m_createInstanceBtn);
    instBtns1->addWidget(m_deleteInstanceBtn);
    middlePanel->addLayout(instBtns1);
    
    m_cloneInstanceBtn = new QPushButton("Clone Instance", this);
    middlePanel->addWidget(m_cloneInstanceBtn);
    
    auto* instBtns2 = new QHBoxLayout();
    m_importInstanceBtn = new QPushButton("Import", this);
    m_exportInstanceBtn = new QPushButton("Export", this);
    instBtns2->addWidget(m_importInstanceBtn);
    instBtns2->addWidget(m_exportInstanceBtn);
    middlePanel->addLayout(instBtns2);
    
    mainLayout->addLayout(middlePanel);
    
    // Separator 2
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep2);
    
    // --------------------------------------------------
    // PANEL 3: Game Setup Form & Console (Right)
    // --------------------------------------------------
    auto* rightPanel = new QVBoxLayout();
    rightPanel->setSpacing(10);
    
    // Header Row
    auto* rightHeader = new QHBoxLayout();
    auto* titleLabel = new QLabel("FLINT", this);
    titleLabel->setStyleSheet("font-weight: 900; font-size: 22px; letter-spacing: 3px; color: #89b4fa;");
    
    m_themeBtn = new QPushButton("Theme", this);
    m_themeBtn->setFixedWidth(80);
    
    rightHeader->addWidget(titleLabel);
    rightHeader->addStretch();
    rightHeader->addWidget(m_themeBtn);
    rightPanel->addLayout(rightHeader);
    
    // Selected Instance Settings Form
    auto* formLayout = new QGridLayout();
    formLayout->setVerticalSpacing(8);
    formLayout->setHorizontalSpacing(8);
    
    formLayout->addWidget(new QLabel("Instance Name:", this), 0, 0);
    m_instNameField = new QLineEdit(this);
    formLayout->addWidget(m_instNameField, 0, 1);
    
    formLayout->addWidget(new QLabel("Version:", this), 1, 0);
    m_versionCombo = new QComboBox(this);
    m_versionCombo->setMinimumWidth(200);
    formLayout->addWidget(m_versionCombo, 1, 1);
    
    formLayout->addWidget(new QLabel("Mod Loader:", this), 2, 0);
    m_loaderCombo = new QComboBox(this);
    m_loaderCombo->addItem("Vanilla");
    m_loaderCombo->addItem("Fabric");
    formLayout->addWidget(m_loaderCombo, 2, 1);
    
    formLayout->addWidget(new QLabel("Allocated RAM:", this), 3, 0);
    auto* sliderLayout = new QHBoxLayout();
    m_ramSlider = new QSlider(Qt::Horizontal, this);
    m_ramSlider->setRange(1024, 16384);
    m_ramSlider->setValue(2048);
    m_ramSlider->setSingleStep(512);
    m_ramSlider->setPageStep(1024);
    m_ramValueLabel = new QLabel("2048 MB", this);
    m_ramValueLabel->setFixedWidth(70);
    
    sliderLayout->addWidget(m_ramSlider);
    sliderLayout->addWidget(m_ramValueLabel);
    formLayout->addLayout(sliderLayout, 3, 1);
    
    formLayout->addWidget(new QLabel("Notes / Flags:", this), 4, 0);
    m_instNotesField = new QLineEdit(this);
    m_instNotesField->setPlaceholderText("Additional Notes");
    formLayout->addWidget(m_instNotesField, 4, 1);
    
    // Linux Options Checkboxes Row
    m_waylandCheck = new QCheckBox("Wayland GLFW", this);
    m_mangohudCheck = new QCheckBox("MangoHud", this);
    m_gamemodeCheck = new QCheckBox("GameMode", this);
    
    auto* linuxOpts = new QHBoxLayout();
    linuxOpts->addWidget(m_waylandCheck);
    linuxOpts->addWidget(m_mangohudCheck);
    linuxOpts->addWidget(m_gamemodeCheck);
    
    formLayout->addWidget(new QLabel("Linux Tweaks:", this), 5, 0);
    formLayout->addLayout(linuxOpts, 5, 1);
    
    rightPanel->addLayout(formLayout);
    
    // Actions Row (Save Configuration & Manage Mods side-by-side)
    auto* formActions = new QHBoxLayout();
    m_saveSettingsBtn = new QPushButton("Save Config", this);
    m_saveSettingsBtn->setFixedHeight(32);
    m_manageModsBtn = new QPushButton("Manage Mods", this);
    m_manageModsBtn->setFixedHeight(32);
    m_manageModsBtn->setStyleSheet("background-color: #89b4fa; color: #11111b; font-weight: bold;");
    
    formActions->addWidget(m_saveSettingsBtn);
    formActions->addWidget(m_manageModsBtn);
    rightPanel->addLayout(formActions);
    
    // Play button & status progress
    m_playBtn = new QPushButton("PLAY INSTANCE", this);
    m_playBtn->setObjectName("playBtn");
    m_playBtn->setFixedHeight(44);
    rightPanel->addWidget(m_playBtn);
    
    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
    rightPanel->addWidget(m_statusLabel);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(8);
    m_progressBar->hide();
    rightPanel->addWidget(m_progressBar);
    
    // Monospace Console Log Panel
    m_consoleLog = new QTextEdit(this);
    m_consoleLog->setReadOnly(true);
    m_consoleLog->setPlaceholderText("Game output will appear here...");
    rightPanel->addWidget(m_consoleLog);
    
    mainLayout->addLayout(rightPanel);
    
    // Widget signals connections
    connect(m_addAccountBtn, &QPushButton::clicked, this, &MainWindow::on_add_account_clicked);
    connect(m_removeAccountBtn, &QPushButton::clicked, this, &MainWindow::on_remove_account_clicked);
    connect(m_msLoginBtn, &QPushButton::clicked, this, &MainWindow::on_ms_login_clicked);
    connect(m_accountsList, &QListWidget::currentRowChanged, this, &MainWindow::on_account_selected);
    
    connect(m_instancesList, &QListWidget::currentRowChanged, this, &MainWindow::on_instance_selected);
    connect(m_createInstanceBtn, &QPushButton::clicked, this, &MainWindow::on_create_instance_clicked);
    connect(m_deleteInstanceBtn, &QPushButton::clicked, this, &MainWindow::on_delete_instance_clicked);
    connect(m_cloneInstanceBtn, &QPushButton::clicked, this, &MainWindow::on_clone_instance_clicked);
    connect(m_exportInstanceBtn, &QPushButton::clicked, this, &MainWindow::on_export_instance_clicked);
    connect(m_importInstanceBtn, &QPushButton::clicked, this, &MainWindow::on_import_instance_clicked);
    connect(m_saveSettingsBtn, &QPushButton::clicked, this, &MainWindow::on_save_instance_settings_clicked);
    connect(m_manageModsBtn, &QPushButton::clicked, this, &MainWindow::on_manage_mods_clicked);
    
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::on_play_clicked);
    connect(m_themeBtn, &QPushButton::clicked, this, &MainWindow::on_theme_toggle_clicked);
    
    connect(m_ramSlider, &QSlider::valueChanged, this, [this](int val) {
        m_ramValueLabel->setText(QString("%1 MB").arg(val));
    });
}

void MainWindow::apply_theme(bool dark) {
    if (dark) {
        setStyleSheet(R"(
            QMainWindow { background-color: #1e1e2e; }
            QWidget { font-family: 'monospace'; font-size: 13px; color: #cdd6f4; }
            QListWidget { background-color: #11111b; border: 1px solid #45475a; border-radius: 4px; }
            QListWidget::item { padding: 8px; border-bottom: 1px solid #313244; color: #cdd6f4; }
            QListWidget::item:selected { background-color: #89b4fa; color: #11111b; border-radius: 2px; }
            QLineEdit, QComboBox { background-color: #11111b; border: 1px solid #45475a; border-radius: 4px; padding: 6px; color: #cdd6f4; }
            QComboBox::drop-down { border: none; }
            QPushButton { background-color: #313244; border: 1px solid #45475a; border-radius: 4px; padding: 6px 12px; font-weight: bold; color: #cdd6f4; }
            QPushButton:hover { background-color: #45475a; }
            QPushButton:pressed { background-color: #585b70; }
            QPushButton#playBtn { background-color: #a6e3a1; color: #11111b; font-size: 15px; font-weight: 900; letter-spacing: 1px; border: none; }
            QPushButton#playBtn:hover { background-color: #b4befe; }
            QPushButton#playBtn:disabled { background-color: #45475a; color: #7f849c; }
            QProgressBar { border: 1px solid #45475a; border-radius: 4px; text-align: center; background-color: #11111b; }
            QProgressBar::chunk { background-color: #89b4fa; }
            QTextEdit { background-color: #11111b; border: 1px solid #45475a; border-radius: 4px; font-family: monospace; font-size: 12px; color: #a6e3a1; }
            QSlider::groove:horizontal { border: 1px solid #45475a; height: 6px; background: #11111b; border-radius: 3px; }
            QSlider::handle:horizontal { background: #89b4fa; border: 1px solid #45475a; width: 14px; margin: -4px 0; border-radius: 7px; }
            QCheckBox { spacing: 5px; color: #cdd6f4; }
            QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #45475a; border-radius: 3px; background-color: #11111b; }
            QCheckBox::indicator:checked { background-color: #a6e3a1; border-color: #a6e3a1; }
        )");
    } else {
        setStyleSheet(R"(
            QMainWindow { background-color: #eff1f5; }
            QWidget { font-family: 'monospace'; font-size: 13px; color: #4c4f69; }
            QListWidget { background-color: #e6e9ef; border: 1px solid #bcc0cc; border-radius: 4px; }
            QListWidget::item { padding: 8px; border-bottom: 1px solid #ccd0da; color: #4c4f69; }
            QListWidget::item:selected { background-color: #1e66f5; color: #eff1f5; border-radius: 2px; }
            QLineEdit, QComboBox { background-color: #e6e9ef; border: 1px solid #bcc0cc; border-radius: 4px; padding: 6px; color: #4c4f69; }
            QComboBox::drop-down { border: none; }
            QPushButton { background-color: #ccd0da; border: 1px solid #bcc0cc; border-radius: 4px; padding: 6px 12px; font-weight: bold; color: #4c4f69; }
            QPushButton:hover { background-color: #bcc0cc; }
            QPushButton:pressed { background-color: #acb0be; }
            QPushButton#playBtn { background-color: #40a02b; color: #eff1f5; font-size: 15px; font-weight: 900; letter-spacing: 1px; border: none; }
            QPushButton#playBtn:hover { background-color: #1e66f5; }
            QPushButton#playBtn:disabled { background-color: #bcc0cc; color: #9ca0b0; }
            QProgressBar { border: 1px solid #bcc0cc; border-radius: 4px; text-align: center; background-color: #e6e9ef; }
            QProgressBar::chunk { background-color: #1e66f5; }
            QTextEdit { background-color: #e6e9ef; border: 1px solid #bcc0cc; border-radius: 4px; font-family: monospace; font-size: 12px; color: #40a02b; }
            QSlider::groove:horizontal { border: 1px solid #bcc0cc; height: 6px; background: #e6e9ef; border-radius: 3px; }
            QSlider::handle:horizontal { background: #1e66f5; border: 1px solid #bcc0cc; width: 14px; margin: -4px 0; border-radius: 7px; }
            QCheckBox { spacing: 5px; color: #4c4f69; }
            QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #bcc0cc; border-radius: 3px; background-color: #e6e9ef; }
            QCheckBox::indicator:checked { background-color: #40a02b; border-color: #40a02b; }
        )");
    }
}

void MainWindow::refresh_accounts_ui() {
    m_accountsList->clear();
    const auto& accounts = m_accountManager.get_accounts();
    const auto* active = m_accountManager.get_active_account();
    
    int activeRow = -1;
    for (int i = 0; i < accounts.size(); ++i) {
        QString displayName = accounts[i].username;
        if (accounts[i].type == "microsoft") {
            displayName += " (MS)";
        }
        m_accountsList->addItem(displayName);
        if (active && active->uuid == accounts[i].uuid) {
            activeRow = i;
        }
    }
    
    if (activeRow != -1) {
        m_accountsList->setCurrentRow(activeRow);
    }
}

void MainWindow::refresh_instances_ui() {
    m_instancesList->clear();
    auto instances = m_instanceManager.get_instances();
    for (const auto& inst : instances) {
        m_instancesList->addItem(inst.name);
    }
}

void MainWindow::on_add_account_clicked() {
    QString username = m_usernameInput->text().trimmed();
    if (username.isEmpty()) {
        QMessageBox::warning(this, "Empty Name", "Please enter an offline username.");
        return;
    }
    m_accountManager.add_offline_account(username);
    m_usernameInput->clear();
    refresh_accounts_ui();
}

void MainWindow::on_remove_account_clicked() {
    int curRow = m_accountsList->currentRow();
    if (curRow < 0) return;
    
    const auto& accounts = m_accountManager.get_accounts();
    if (curRow < accounts.size()) {
        m_accountManager.remove_account(accounts[curRow].uuid);
        refresh_accounts_ui();
    }
}

void MainWindow::on_ms_login_clicked() {
    MicrosoftLoginDialog dlg(m_accountManager, this);
    if (dlg.exec() == QDialog::Accepted) {
        refresh_accounts_ui();
    }
}

void MainWindow::on_account_selected(int index) {
    const auto& accounts = m_accountManager.get_accounts();
    if (index >= 0 && index < accounts.size()) {
        m_accountManager.set_active_account(accounts[index].uuid);
    }
}

void MainWindow::on_instance_selected(int index) {
    if (index < 0) return;
    QString name = m_instancesList->item(index)->text();
    load_selected_instance_details(name);
}

void MainWindow::load_selected_instance_details(const QString& name) {
    minecraft::Instance inst;
    if (minecraft::InstanceManager::load_instance_config(name, inst)) {
        m_instNameField->setText(inst.name);
        m_instNotesField->setText(inst.notes);
        m_ramSlider->setValue(inst.maxMemory);
        m_ramValueLabel->setText(QString("%1 MB").arg(inst.maxMemory));
        
        m_loaderCombo->setCurrentText(inst.loader == "fabric" ? "Fabric" : "Vanilla");
        
        m_waylandCheck->setChecked(inst.useWayland);
        m_mangohudCheck->setChecked(inst.useMangoHud);
        m_gamemodeCheck->setChecked(inst.useGameMode);
        
        int idx = m_versionCombo->findText(inst.mcVersion);
        if (idx >= 0) {
            m_versionCombo->setCurrentIndex(idx);
        } else {
            if (m_versionCombo->findText(inst.mcVersion) < 0) {
                m_versionCombo->addItem(inst.mcVersion);
            }
            m_versionCombo->setCurrentText(inst.mcVersion);
        }
    }
}

void MainWindow::on_save_instance_settings_clicked() {
    int curRow = m_instancesList->currentRow();
    if (curRow < 0) return;
    
    QString oldName = m_instancesList->item(curRow)->text();
    QString newName = m_instNameField->text().trimmed();
    
    if (newName.isEmpty()) {
        QMessageBox::warning(this, "Invalid Name", "Instance name cannot be empty.");
        return;
    }
    
    minecraft::Instance inst;
    if (!minecraft::InstanceManager::load_instance_config(oldName, inst)) return;
    
    inst.mcVersion = m_versionCombo->currentText();
    inst.loader = m_loaderCombo->currentText().toLower();
    inst.maxMemory = m_ramSlider->value();
    inst.notes = m_instNotesField->text().trimmed();
    
    inst.useWayland = m_waylandCheck->isChecked();
    inst.useMangoHud = m_mangohudCheck->isChecked();
    inst.useGameMode = m_gamemodeCheck->isChecked();
    
    if (newName != oldName) {
        // Check duplicate directories
        QDir instancesDir(QString::fromStdString(flint::fs::get_instances_dir()));
        if (instancesDir.exists(newName)) {
            QMessageBox::warning(this, "Duplicate Name", "An instance with that name already exists.");
            return;
        }
        
        // Rename folder on disk
        std::filesystem::path oldPath = flint::fs::get_instances_dir() + "/" + oldName.toStdString();
        std::filesystem::path newPath = flint::fs::get_instances_dir() + "/" + newName.toStdString();
        try {
            std::filesystem::rename(oldPath, newPath);
            inst.name = newName;
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Rename Failed", QString("Failed to rename directory: %1").arg(e.what()));
            return;
        }
    }
    
    m_instanceManager.save_instance(inst);
    
    // Check if Fabric selected but fabric_profile.json not downloaded
    if (inst.loader == "fabric") {
        QString fabricProfilePath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + inst.name + "/fabric_profile.json";
        if (!QFileInfo::exists(fabricProfilePath)) {
            m_statusLabel->setText("Downloading Fabric loader metadata...");
            m_statusLabel->setStyleSheet("color: #f9e2af;");
            m_playBtn->setEnabled(false);
            m_saveSettingsBtn->setEnabled(false);
            m_manageModsBtn->setEnabled(false);
            
            QtConcurrent::run([this, mcVer = inst.mcVersion, name = inst.name, inst]() {
                bool ok = m_minecraftManager.install_fabric(mcVer, name);
                QMetaObject::invokeMethod(this, [this, ok, inst]() {
                    m_playBtn->setEnabled(true);
                    m_saveSettingsBtn->setEnabled(true);
                    m_manageModsBtn->setEnabled(true);
                    if (ok) {
                        m_statusLabel->setText("Fabric loader installed successfully.");
                        m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
                        m_instanceManager.save_instance(inst);
                    } else {
                        m_statusLabel->setText("Failed to install Fabric loader.");
                        m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
                    }
                });
            });
        }
    } else {
        // If switched back to vanilla, clean up the fabric_profile.json file to disable it
        QString fabricProfilePath = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + inst.name + "/fabric_profile.json";
        if (QFileInfo::exists(fabricProfilePath)) {
            QFile::remove(fabricProfilePath);
        }
        m_statusLabel->setText("Instance settings saved.");
    }
    
    refresh_instances_ui();
    
    // Select the renamed row
    for (int i = 0; i < m_instancesList->count(); ++i) {
        if (m_instancesList->item(i)->text() == inst.name) {
            m_instancesList->setCurrentRow(i);
            break;
        }
    }
}

void MainWindow::on_manage_mods_clicked() {
    int curRow = m_instancesList->currentRow();
    if (curRow < 0) {
        QMessageBox::warning(this, "No Selected Instance", "Please select an instance first.");
        return;
    }
    
    QString instName = m_instancesList->item(curRow)->text();
    minecraft::Instance inst;
    if (minecraft::InstanceManager::load_instance_config(instName, inst)) {
        ModManagerDialog dlg(inst, this);
        dlg.exec();
    }
}

void MainWindow::on_create_instance_clicked() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Instance", "Enter instance name:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    
    QDir instancesDir(QString::fromStdString(flint::fs::get_instances_dir()));
    if (instancesDir.exists(name)) {
        QMessageBox::warning(this, "Duplicate Name", "An instance with that name already exists.");
        return;
    }
    
    minecraft::Instance inst;
    inst.name = name;
    inst.mcVersion = m_versionCombo->currentText();
    if (inst.mcVersion.isEmpty()) inst.mcVersion = "1.20.4";
    
    m_instanceManager.create_instance(inst);
    refresh_instances_ui();
    
    // Select the new instance
    for (int i = 0; i < m_instancesList->count(); ++i) {
        if (m_instancesList->item(i)->text() == name) {
            m_instancesList->setCurrentRow(i);
            break;
        }
    }
}

void MainWindow::on_delete_instance_clicked() {
    int curRow = m_instancesList->currentRow();
    if (curRow < 0) return;
    
    QString name = m_instancesList->item(curRow)->text();
    auto btn = QMessageBox::question(this, "Delete Instance", 
                                     QString("Are you sure you want to permanently delete instance '%1' and all its worlds/saves?").arg(name),
                                     QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;
    
    m_instanceManager.delete_instance(name);
    refresh_instances_ui();
    if (m_instancesList->count() > 0) {
        m_instancesList->setCurrentRow(0);
    }
}

void MainWindow::on_clone_instance_clicked() {
    int curRow = m_instancesList->currentRow();
    if (curRow < 0) return;
    
    QString srcName = m_instancesList->item(curRow)->text();
    bool ok = false;
    QString destName = QInputDialog::getText(this, "Clone Instance", QString("Enter clone name for '%1':").arg(srcName), QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || destName.isEmpty()) return;
    
    QDir instancesDir(QString::fromStdString(flint::fs::get_instances_dir()));
    if (instancesDir.exists(destName)) {
        QMessageBox::warning(this, "Duplicate Name", "An instance with that name already exists.");
        return;
    }
    
    m_statusLabel->setText("Cloning instance... please wait.");
    m_statusLabel->setStyleSheet("color: #f9e2af;");
    
    QtConcurrent::run([this, srcName, destName]() {
        bool success = m_instanceManager.clone_instance(srcName, destName);
        QMetaObject::invokeMethod(this, [this, success, destName]() {
            if (success) {
                refresh_instances_ui();
                m_statusLabel->setText("Instance cloned successfully.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
                for (int i = 0; i < m_instancesList->count(); ++i) {
                    if (m_instancesList->item(i)->text() == destName) {
                        m_instancesList->setCurrentRow(i);
                        break;
                    }
                }
            } else {
                m_statusLabel->setText("Cloning failed.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
            }
        });
    });
}

void MainWindow::on_export_instance_clicked() {
    int curRow = m_instancesList->currentRow();
    if (curRow < 0) return;
    
    QString name = m_instancesList->item(curRow)->text();
    QString zipPath = QFileDialog::getSaveFileName(this, "Export Instance", QDir::homePath() + "/" + name + ".zip", "Zip Archives (*.zip)");
    if (zipPath.isEmpty()) return;
    
    m_statusLabel->setText("Exporting instance... please wait.");
    m_statusLabel->setStyleSheet("color: #f9e2af;");
    
    QtConcurrent::run([this, name, zipPath]() {
        bool success = m_instanceManager.export_instance(name, zipPath);
        QMetaObject::invokeMethod(this, [this, success]() {
            if (success) {
                m_statusLabel->setText("Instance exported successfully.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
            } else {
                m_statusLabel->setText("Export failed.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
            }
        });
    });
}

void MainWindow::on_import_instance_clicked() {
    QString zipPath = QFileDialog::getOpenFileName(this, "Import Instance", QDir::homePath(), "Zip Archives (*.zip)");
    if (zipPath.isEmpty()) return;
    
    QFileInfo fi(zipPath);
    QString defaultName = fi.baseName();
    
    bool ok = false;
    QString name = QInputDialog::getText(this, "Import Instance", "Enter imported instance name:", QLineEdit::Normal, defaultName, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    
    QDir instancesDir(QString::fromStdString(flint::fs::get_instances_dir()));
    if (instancesDir.exists(name)) {
        QMessageBox::warning(this, "Duplicate Name", "An instance with that name already exists.");
        return;
    }
    
    m_statusLabel->setText("Importing instance... please wait.");
    m_statusLabel->setStyleSheet("color: #f9e2af;");
    
    QtConcurrent::run([this, zipPath, name]() {
        bool success = m_instanceManager.import_instance(zipPath, name);
        QMetaObject::invokeMethod(this, [this, success, name]() {
            if (success) {
                refresh_instances_ui();
                m_statusLabel->setText("Instance imported successfully.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
                for (int i = 0; i < m_instancesList->count(); ++i) {
                    if (m_instancesList->item(i)->text() == name) {
                        m_instancesList->setCurrentRow(i);
                        break;
                    }
                }
            } else {
                m_statusLabel->setText("Import failed.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
            }
        });
    });
}

void MainWindow::on_manifest_loaded(bool success) {
    if (success) {
        m_versionCombo->clear();
        auto versions = m_minecraftManager.get_versions(false);
        for (const auto& v : versions) {
            m_versionCombo->addItem(v.id);
        }
        
        // Reload selected instance details to match its version in the combo list
        int curRow = m_instancesList->currentRow();
        if (curRow >= 0) {
            load_selected_instance_details(m_instancesList->item(curRow)->text());
        }
        
        m_playBtn->setEnabled(true);
        m_statusLabel->setText("Ready");
        m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
    } else {
        m_statusLabel->setText("Failed to load version manifest.");
        m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
    }
}

void MainWindow::on_theme_toggle_clicked() {
    m_isDark = !m_isDark;
    apply_theme(m_isDark);
}

void MainWindow::on_play_clicked() {
    int curRow = m_instancesList->currentRow();
    if (curRow < 0) {
        m_statusLabel->setText("Error: Select an instance first.");
        return;
    }
    
    QString instName = m_instancesList->item(curRow)->text();
    minecraft::Instance inst;
    if (!minecraft::InstanceManager::load_instance_config(instName, inst)) {
        m_statusLabel->setText("Error: Failed to load instance config.");
        return;
    }
    
    QString selectedVer = inst.mcVersion;
    if (selectedVer.isEmpty()) {
        m_statusLabel->setText("Error: Instance version is empty.");
        return;
    }
    
    const auto* acc = m_accountManager.get_active_account();
    if (!acc) {
        m_statusLabel->setText("Error: Select an account first.");
        m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
        return;
    }
    
    m_playBtn->setEnabled(false);
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_statusLabel->setText("Initializing game launch...");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #f9e2af;");
    
    m_consoleLog->clear();
    m_consoleLog->append("------------ PREPARING LAUNCH ------------");
    m_consoleLog->append(QString("Instance: %1").arg(inst.name));
    m_consoleLog->append(QString("Version:  %1").arg(selectedVer));
    m_consoleLog->append(QString("Loader:   %1").arg(inst.loader));
    
    // Asynchronous prepare and launch workflow
    QtConcurrent::run([this, selectedVer, inst, ramMb = inst.maxMemory]() {
        // Step 0: Refresh Microsoft Token if necessary
        QMetaObject::invokeMethod(this, [this]() {
            m_statusLabel->setText("Refreshing login session...");
        });
        
        bool refreshOk = m_accountManager.refresh_active_account_token();
        if (!refreshOk) {
            QMetaObject::invokeMethod(this, [this]() {
                m_statusLabel->setText("Session expired. Please log in again.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
                m_playBtn->setEnabled(true);
                m_progressBar->hide();
            });
            return;
        }
        
        accounts::Account activeAcc = *m_accountManager.get_active_account();
        
        // Step 1: Check and download required Java version from Mojang metadata JSON
        QMetaObject::invokeMethod(this, [this]() {
            m_statusLabel->setText("Resolving version requirements...");
        });
        
        int reqJava = m_minecraftManager.get_required_java_version(selectedVer);
        
        QMetaObject::invokeMethod(this, [this, reqJava]() {
            m_statusLabel->setText(QString("Resolving Java %1 runtime...").arg(reqJava));
        });
        
        QString javaPath = m_javaManager.get_or_download_java(reqJava, [this, reqJava](int pct, const QString& status) {
            QMetaObject::invokeMethod(this, [this, pct, status]() {
                m_progressBar->setValue(pct / 2);
                m_statusLabel->setText(status);
            });
        });
        
        if (javaPath.isEmpty()) {
            QMetaObject::invokeMethod(this, [this]() {
                m_statusLabel->setText("Java runtime installation failed.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
                m_playBtn->setEnabled(true);
                m_progressBar->hide();
            });
            return;
        }
        
        // Step 2: Prepare Minecraft directories, client jar, assets, libraries
        QMetaObject::invokeMethod(this, [this]() {
            m_statusLabel->setText("Preparing Minecraft assets & libraries...");
        });
        
        bool success = m_minecraftManager.prepare_version(selectedVer, inst.name, [this](int pct, const QString& status) {
            QMetaObject::invokeMethod(this, [this, pct, status]() {
                m_progressBar->setValue(50 + (pct / 2));
                m_statusLabel->setText(status);
            });
        });
        
        if (!success) {
            QMetaObject::invokeMethod(this, [this]() {
                m_statusLabel->setText("Failed to download assets/libraries.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
                m_playBtn->setEnabled(true);
                m_progressBar->hide();
            });
            return;
        }
        
        // Step 3: Trigger game launch inside QProcess (needs to run on main thread)
        QMetaObject::invokeMethod(this, [this, selectedVer, inst, activeAcc, javaPath, ramMb]() {
            m_statusLabel->setText("Launching Minecraft...");
            m_progressBar->hide();
            
            m_gameProcess = m_minecraftManager.launch_game(selectedVer, inst.name, activeAcc, javaPath, ramMb);
            if (m_gameProcess) {
                connect(m_gameProcess, &QProcess::started, this, &MainWindow::on_game_started);
                connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::on_game_stdout);
                connect(m_gameProcess, &QProcess::readyReadStandardError, this, &MainWindow::on_game_stderr);
                connect(m_gameProcess, &QProcess::finished, this, &MainWindow::on_game_finished);
            } else {
                m_statusLabel->setText("Launch execution failed.");
                m_statusLabel->setStyleSheet("font-weight: bold; color: #f38ba8;");
                m_playBtn->setEnabled(true);
            }
        });
    });
}

void MainWindow::on_game_started() {
    m_statusLabel->setText("Minecraft is running...");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
    m_consoleLog->append("Game started successfully.");
}

void MainWindow::on_game_stdout() {
    if (m_gameProcess) {
        QByteArray data = m_gameProcess->readAllStandardOutput();
        m_consoleLog->insertPlainText(QString::fromUtf8(data));
        m_consoleLog->verticalScrollBar()->setValue(m_consoleLog->verticalScrollBar()->maximum());
    }
}

void MainWindow::on_game_stderr() {
    if (m_gameProcess) {
        QByteArray data = m_gameProcess->readAllStandardError();
        m_consoleLog->insertPlainText(QString::fromUtf8(data));
        m_consoleLog->verticalScrollBar()->setValue(m_consoleLog->verticalScrollBar()->maximum());
    }
}

void MainWindow::on_game_finished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_playBtn->setEnabled(true);
    m_statusLabel->setText("Ready");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #a6e3a1;");
    
    m_consoleLog->append(QString("\n------------ GAME TERMINATED ------------\nExit code: %1 (Status: %2)")
                         .arg(exitCode)
                         .arg(exitStatus == QProcess::NormalExit ? "Normal" : "Crash"));
    
    m_gameProcess->deleteLater();
    m_gameProcess = nullptr;
}

} // namespace flint::ui
