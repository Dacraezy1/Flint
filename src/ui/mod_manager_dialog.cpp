#include "mod_manager_dialog.hpp"
#include "network/network.hpp"
#include "logging/logging.hpp"
#include "filesystem/filesystem.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace flint::ui {

namespace {
bool download_mod_by_slug(const QString& slug, const QString& mcVer, const QString& loader, const QString& modsDir) {
    QString versionUrl = QString("https://api.modrinth.com/v2/project/%1/version").arg(slug);
    bool ok = false;
    QByteArray resData = flint::network::get(versionUrl, {}, &ok);
    if (!ok || resData.isEmpty()) return false;
    
    try {
        json j = json::parse(resData.constData());
        if (!j.is_array()) return false;
        
        std::string downloadUrl;
        std::string filename;
        
        for (const auto& ver : j) {
            bool gameMatch = false;
            if (ver.contains("game_versions") && ver["game_versions"].is_array()) {
                for (const auto& gv : ver["game_versions"]) {
                    if (gv.get<std::string>() == mcVer.toStdString()) {
                        gameMatch = true;
                        break;
                    }
                }
            }
            
            bool loaderMatch = false;
            if (ver.contains("loaders") && ver["loaders"].is_array()) {
                for (const auto& ld : ver["loaders"]) {
                    if (ld.get<std::string>() == loader.toStdString() || loader == "vanilla") {
                        loaderMatch = true;
                        break;
                    }
                }
            }
            
            if (gameMatch && loaderMatch) {
                if (ver.contains("files") && ver["files"].is_array() && !ver["files"].empty()) {
                    auto fileNode = ver["files"][0];
                    for (const auto& f : ver["files"]) {
                        if (f.value("primary", false)) {
                            fileNode = f;
                            break;
                        }
                    }
                    downloadUrl = fileNode.value("url", "");
                    filename = fileNode.value("filename", "");
                    break;
                }
            }
        }
        
        if (downloadUrl.empty()) return false;
        
        QString qDest = modsDir + "/" + QString::fromStdString(filename);
        return flint::network::download_file(QString::fromStdString(downloadUrl), qDest);
    } catch (...) {
        return false;
    }
}
}

ModManagerDialog::ModManagerDialog(const minecraft::Instance& inst, QWidget* parent)
    : QDialog(parent), m_instance(inst) {
    setWindowTitle(QString("Manage Mods - %1").arg(inst.name));
    setModal(true);
    resize(650, 450);
    
    // Ensure mods directory exists
    m_modsDir = QString::fromStdString(flint::fs::get_instances_dir()) + "/" + inst.name + "/mods";
    QDir().mkpath(m_modsDir);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);
    
    m_tabWidget = new QTabWidget(this);
    
    // --------------------------------------------------
    // TAB 1: Installed Mods
    // --------------------------------------------------
    auto* tab1 = new QWidget(this);
    auto* tab1Layout = new QVBoxLayout(tab1);
    
    m_installedList = new QListWidget(this);
    tab1Layout->addWidget(m_installedList);
    
    auto* t1Btns = new QHBoxLayout();
    m_addLocalBtn = new QPushButton("Add Mod (Local Jar...)", this);
    m_deleteModBtn = new QPushButton("Delete Selected Mod", this);
    m_installPresetBtn = new QPushButton("One-Click Optimize (Sodium)", this);
    m_installPresetBtn->setStyleSheet("background-color: #a6e3a1; color: #11111b; font-weight: bold;");
    
    t1Btns->addWidget(m_addLocalBtn);
    t1Btns->addWidget(m_deleteModBtn);
    t1Btns->addWidget(m_installPresetBtn);
    tab1Layout->addLayout(t1Btns);
    
    m_tabWidget->addTab(tab1, "Installed Mods");
    
    // --------------------------------------------------
    // TAB 2: Download Mods (Modrinth)
    // --------------------------------------------------
    auto* tab2 = new QWidget(this);
    auto* tab2Layout = new QVBoxLayout(tab2);
    
    auto* searchRow = new QHBoxLayout();
    m_searchBar = new QLineEdit(this);
    m_searchBar->setPlaceholderText("Search Modrinth...");
    m_searchBtn = new QPushButton("Search", this);
    searchRow->addWidget(m_searchBar);
    searchRow->addWidget(m_searchBtn);
    tab2Layout->addLayout(searchRow);
    
    auto* resultsLayout = new QHBoxLayout();
    m_searchResultsList = new QListWidget(this);
    m_searchResultsList->setFixedWidth(250);
    
    m_modDescriptionLabel = new QLabel("Select a mod to view details.", this);
    m_modDescriptionLabel->setWordWrap(true);
    m_modDescriptionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_modDescriptionLabel->setStyleSheet("padding: 8px; border: 1px solid #45475a; border-radius: 4px; background-color: #11111b; font-size: 12px;");
    
    resultsLayout->addWidget(m_searchResultsList);
    resultsLayout->addWidget(m_modDescriptionLabel);
    tab2Layout->addLayout(resultsLayout);
    
    m_downloadBtn = new QPushButton("Download and Install Mod", this);
    m_downloadBtn->setEnabled(false);
    tab2Layout->addWidget(m_downloadBtn);
    
    m_tabWidget->addTab(tab2, "Search Modrinth");
    mainLayout->addWidget(m_tabWidget);
    
    // Bottom Close
    auto* closeRow = new QHBoxLayout();
    auto* closeBtn = new QPushButton("Close", this);
    closeRow->addStretch();
    closeRow->addWidget(closeBtn);
    mainLayout->addLayout(closeRow);
    
    // Styles
    apply_styles();
    
    // Connects
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_addLocalBtn, &QPushButton::clicked, this, &ModManagerDialog::on_add_local_mods);
    connect(m_deleteModBtn, &QPushButton::clicked, this, &ModManagerDialog::on_delete_selected_mod);
    connect(m_installPresetBtn, &QPushButton::clicked, this, &ModManagerDialog::on_install_preset_clicked);
    
    connect(m_searchBtn, &QPushButton::clicked, this, &ModManagerDialog::on_search_mods);
    connect(m_searchBar, &QLineEdit::returnPressed, this, &ModManagerDialog::on_search_mods);
    connect(m_searchResultsList, &QListWidget::currentRowChanged, this, &ModManagerDialog::on_search_result_selection_changed);
    connect(m_downloadBtn, &QPushButton::clicked, this, &ModManagerDialog::on_download_selected_mod);
    
    // Initial Load
    refresh_installed_mods();
}

void ModManagerDialog::apply_styles() {
    setStyleSheet(parentWidget() ? parentWidget()->styleSheet() : "");
}

void ModManagerDialog::refresh_installed_mods() {
    m_installedList->clear();
    QDir dir(m_modsDir);
    QStringList files = dir.entryList(QStringList() << "*.jar", QDir::Files);
    for (const auto& f : files) {
        m_installedList->addItem(f);
    }
}

void ModManagerDialog::on_add_local_mods() {
    QStringList paths = QFileDialog::getOpenFileNames(this, "Select Mod Jars", QDir::homePath(), "Java Archives (*.jar)");
    if (paths.isEmpty()) return;
    
    for (const auto& src : paths) {
        QFileInfo fi(src);
        QString dest = m_modsDir + "/" + fi.fileName();
        if (QFile::exists(dest)) {
            QFile::remove(dest);
        }
        QFile::copy(src, dest);
    }
    
    refresh_installed_mods();
}

void ModManagerDialog::on_delete_selected_mod() {
    int curRow = m_installedList->currentRow();
    if (curRow < 0) return;
    
    QString fileName = m_installedList->item(curRow)->text();
    auto res = QMessageBox::question(this, "Delete Mod", 
                                     QString("Are you sure you want to delete '%1'?").arg(fileName), 
                                     QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;
    
    QFile::remove(m_modsDir + "/" + fileName);
    refresh_installed_mods();
}

void ModManagerDialog::on_install_preset_clicked() {
    m_installPresetBtn->setEnabled(false);
    m_installPresetBtn->setText("Optimizing...");
    
    QString mcVer = m_instance.mcVersion;
    QString loader = m_instance.loader.toLower();
    
    QtConcurrent::run([this, mcVer, loader]() {
        // Asynchronously download Sodium, Lithium, and FerriteCore
        bool sod = download_mod_by_slug("sodium", mcVer, loader, m_modsDir);
        bool lith = download_mod_by_slug("lithium", mcVer, loader, m_modsDir);
        bool ferr = download_mod_by_slug("ferritecore", mcVer, loader, m_modsDir);
        
        QMetaObject::invokeMethod(this, [this, sod, lith, ferr]() {
            m_installPresetBtn->setEnabled(true);
            m_installPresetBtn->setText("One-Click Optimize (Sodium)");
            refresh_installed_mods();
            
            int count = (sod ? 1 : 0) + (lith ? 1 : 0) + (ferr ? 1 : 0);
            QMessageBox::information(this, "Optimization Preset Pack", 
                                     QString("Preset processing finished. Installed %1 mods.\n"
                                             "- Sodium: %2\n"
                                             "- Lithium: %3\n"
                                             "- FerriteCore: %4")
                                     .arg(count)
                                     .arg(sod ? "Success" : "Not Found / Incompatible")
                                     .arg(lith ? "Success" : "Not Found / Incompatible")
                                     .arg(ferr ? "Success" : "Not Found / Incompatible"));
        });
    });
}

void ModManagerDialog::on_search_mods() {
    QString query = m_searchBar->text().trimmed();
    if (query.isEmpty()) return;
    
    m_searchBtn->setEnabled(false);
    m_searchBar->setEnabled(false);
    m_searchResultsList->clear();
    m_searchResults.clear();
    m_modDescriptionLabel->setText("Searching Modrinth... please wait.");
    m_downloadBtn->setEnabled(false);
    
    QString mcVer = m_instance.mcVersion;
    QString facets = QString("[[\"categories:mods\"],[\"versions:\"%1\"\"]]").arg(mcVer);
    QString encodedQuery = QUrl::toPercentEncoding(query);
    QString encodedFacets = QUrl::toPercentEncoding(facets);
    
    QString searchUrl = QString("https://api.modrinth.com/v2/search?query=%1&facets=%2")
                        .arg(encodedQuery).arg(encodedFacets);
    
    QtConcurrent::run([this, searchUrl]() {
        bool ok = false;
        QByteArray res = flint::network::get(searchUrl, {}, &ok);
        
        QMetaObject::invokeMethod(this, [this, ok, res]() {
            m_searchBtn->setEnabled(true);
            m_searchBar->setEnabled(true);
            
            if (!ok) {
                m_modDescriptionLabel->setText("Failed to connect to Modrinth API.");
                return;
            }
            
            try {
                json j = json::parse(res.constData());
                if (j.contains("hits") && j["hits"].is_array()) {
                    for (const auto& hit : j["hits"]) {
                        ModrinthResult r;
                        r.projectId = QString::fromStdString(hit.value("project_id", ""));
                        r.title = QString::fromStdString(hit.value("title", ""));
                        r.description = QString::fromStdString(hit.value("description", ""));
                        r.slug = QString::fromStdString(hit.value("slug", ""));
                        
                        m_searchResults.append(r);
                        m_searchResultsList->addItem(r.title);
                    }
                }
                
                if (m_searchResults.isEmpty()) {
                    m_modDescriptionLabel->setText("No compatible mods found on Modrinth.");
                } else {
                    m_modDescriptionLabel->setText("Select a mod from the results to view details.");
                }
            } catch (...) {
                m_modDescriptionLabel->setText("Error parsing search results.");
            }
        });
    });
}

void ModManagerDialog::on_search_result_selection_changed() {
    int curRow = m_searchResultsList->currentRow();
    if (curRow < 0 || curRow >= m_searchResults.size()) {
        m_modDescriptionLabel->setText("");
        m_downloadBtn->setEnabled(false);
        return;
    }
    
    const auto& res = m_searchResults[curRow];
    m_modDescriptionLabel->setText(QString("<b>%1</b><br><br>%2").arg(res.title).arg(res.description));
    m_downloadBtn->setEnabled(true);
}

void ModManagerDialog::on_download_selected_mod() {
    int curRow = m_searchResultsList->currentRow();
    if (curRow < 0 || curRow >= m_searchResults.size()) return;
    
    const auto& res = m_searchResults[curRow];
    m_downloadBtn->setEnabled(false);
    m_modDescriptionLabel->setText(QString("<b>%1</b><br><br>Finding compatible download file...").arg(res.title));
    
    QString projectId = res.projectId;
    QString mcVer = m_instance.mcVersion;
    QString loader = m_instance.loader.toLower();
    
    QString versionUrl = QString("https://api.modrinth.com/v2/project/%1/version").arg(projectId);
    
    QtConcurrent::run([this, versionUrl, mcVer, loader, title = res.title]() {
        bool ok = false;
        QByteArray resData = flint::network::get(versionUrl, {}, &ok);
        
        QMetaObject::invokeMethod(this, [this, ok, resData, mcVer, loader, title]() {
            if (!ok) {
                m_modDescriptionLabel->setText("Failed to fetch project versions.");
                m_downloadBtn->setEnabled(true);
                return;
            }
            
            try {
                json j = json::parse(resData.constData());
                if (!j.is_array() || j.empty()) {
                    m_modDescriptionLabel->setText("No versions found for this project.");
                    m_downloadBtn->setEnabled(true);
                    return;
                }
                
                std::string downloadUrl;
                std::string filename;
                
                for (const auto& ver : j) {
                    bool gameMatch = false;
                    if (ver.contains("game_versions") && ver["game_versions"].is_array()) {
                        for (const auto& gv : ver["game_versions"]) {
                            if (gv.get<std::string>() == mcVer.toStdString()) {
                                gameMatch = true;
                                break;
                            }
                        }
                    }
                    
                    bool loaderMatch = false;
                    if (ver.contains("loaders") && ver["loaders"].is_array()) {
                        for (const auto& ld : ver["loaders"]) {
                            if (ld.get<std::string>() == loader.toStdString() || loader == "vanilla") {
                                loaderMatch = true;
                                break;
                            }
                        }
                    }
                    
                    if (gameMatch && loaderMatch) {
                        if (ver.contains("files") && ver["files"].is_array() && !ver["files"].empty()) {
                            auto fileNode = ver["files"][0];
                            for (const auto& f : ver["files"]) {
                                if (f.value("primary", false)) {
                                    fileNode = f;
                                    break;
                                }
                            }
                            downloadUrl = fileNode.value("url", "");
                            filename = fileNode.value("filename", "");
                            break;
                        }
                    }
                }
                
                if (downloadUrl.empty()) {
                    m_modDescriptionLabel->setText(QString("Could not find version matching Minecraft %1 and loader '%2'.").arg(mcVer).arg(loader));
                    m_downloadBtn->setEnabled(true);
                    return;
                }
                
                m_modDescriptionLabel->setText(QString("<b>%1</b><br><br>Downloading %2...").arg(title).arg(QString::fromStdString(filename)));
                
                QString qUrl = QString::fromStdString(downloadUrl);
                QString qDest = m_modsDir + "/" + QString::fromStdString(filename);
                
                QtConcurrent::run([this, qUrl, qDest, title]() {
                    bool success = flint::network::download_file(qUrl, qDest);
                    QMetaObject::invokeMethod(this, [this, success, title]() {
                        m_downloadBtn->setEnabled(true);
                        if (success) {
                            m_modDescriptionLabel->setText(QString("<b>%1</b><br><br>Mod successfully downloaded and installed!").arg(title));
                            refresh_installed_mods();
                        } else {
                            m_modDescriptionLabel->setText("Download failed.");
                        }
                    });
                });
                
            } catch (...) {
                m_modDescriptionLabel->setText("Error parsing project version data.");
                m_downloadBtn->setEnabled(true);
            }
        });
    });
}

} // namespace flint::ui
