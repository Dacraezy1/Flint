#include "microsoft_login_dialog.hpp"
#include "accounts/microsoft_auth.hpp"
#include "logging/logging.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QDesktopServices>
#include <QUrl>

namespace flint::ui {

MicrosoftLoginDialog::MicrosoftLoginDialog(accounts::AccountManager& accountManager, QWidget* parent)
    : QDialog(parent), m_accountManager(accountManager) {
    setWindowTitle("Microsoft Login");
    setModal(true);
    setFixedSize(420, 260);
    
    // Flat style inherited
    setStyleSheet(parent ? parent->styleSheet() : "");
    
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    
    m_instructionLabel = new QLabel("Requesting authorization code from Microsoft...", this);
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_instructionLabel);
    
    m_codeLabel = new QLabel("--- ---", this);
    m_codeLabel->setAlignment(Qt::AlignCenter);
    m_codeLabel->setStyleSheet("font-family: monospace; font-size: 24px; font-weight: bold; color: #89b4fa; padding: 10px; background-color: #11111b; border-radius: 4px;");
    layout->addWidget(m_codeLabel);
    
    m_statusLabel = new QLabel("Connecting...", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-style: italic; color: #a6e3a1;");
    layout->addWidget(m_statusLabel);
    
    layout->addStretch();
    
    auto* btns = new QHBoxLayout();
    m_cancelBtn = new QPushButton("Cancel", this);
    btns->addStretch();
    btns->addWidget(m_cancelBtn);
    btns->addStretch();
    layout->addLayout(btns);
    
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MicrosoftLoginDialog::on_poll_timer);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MicrosoftLoginDialog::on_cancel_clicked);
    
    // Start device code request in background
    start_device_flow();
}

MicrosoftLoginDialog::~MicrosoftLoginDialog() {
    m_pollTimer->stop();
}

void MicrosoftLoginDialog::start_device_flow() {
    QtConcurrent::run([this]() {
        auto resp = accounts::MicrosoftAuth::request_device_code();
        
        QMetaObject::invokeMethod(this, [this, resp]() {
            if (!resp.error.isEmpty()) {
                update_status("Error: " + resp.error, true);
                m_instructionLabel->setText("Failed to start device auth flow.");
                return;
            }
            
            m_deviceCode = resp.deviceCode;
            m_pollInterval = resp.interval;
            m_timeRemaining = resp.expiresIn;
            
            m_instructionLabel->setText(QString("1. Go to: <b>%1</b><br>2. Enter the code below:").arg(resp.verificationUri));
            m_codeLabel->setText(resp.userCode);
            m_statusLabel->setText("Waiting for Microsoft authorization...");
            m_statusLabel->setStyleSheet("color: #f9e2af;");
            
            // Open link in browser automatically for user convenience
            QDesktopServices::openUrl(QUrl(resp.verificationUri));
            
            // Start polling timer
            m_pollTimer->start(m_pollInterval * 1000);
        });
    });
}

void MicrosoftLoginDialog::on_poll_timer() {
    m_timeRemaining -= m_pollInterval;
    if (m_timeRemaining <= 0) {
        m_pollTimer->stop();
        update_status("Error: Verification code expired.", true);
        return;
    }
    
    // Check if token is ready in a background thread
    QtConcurrent::run([this]() {
        auto tokenResp = accounts::MicrosoftAuth::poll_for_token(m_deviceCode);
        
        QMetaObject::invokeMethod(this, [this, tokenResp]() {
            if (!tokenResp.error.isEmpty()) {
                if (tokenResp.error == "authorization_pending") {
                    // Still waiting, do nothing
                    return;
                }
                
                m_pollTimer->stop();
                update_status("Authentication failed: " + tokenResp.error, true);
            } else {
                m_pollTimer->stop();
                handle_auth_success(tokenResp.accessToken, tokenResp.refreshToken);
            }
        });
    });
}

void MicrosoftLoginDialog::handle_auth_success(const QString& msAccessToken, const QString& msRefreshToken) {
    m_statusLabel->setText("Authenticating with Minecraft Services...");
    m_statusLabel->setStyleSheet("color: #89b4fa;");
    
    QtConcurrent::run([this, msAccessToken, msRefreshToken]() {
        QString mcToken, username, uuid, errorMsg;
        bool ok = accounts::MicrosoftAuth::login_to_minecraft(msAccessToken, mcToken, username, uuid, errorMsg);
        
        QMetaObject::invokeMethod(this, [this, ok, mcToken, username, uuid, msRefreshToken, errorMsg]() {
            if (!ok) {
                update_status("Login failed: " + errorMsg, true);
                return;
            }
            
            flint::logging::info("Successfully logged in Microsoft account: {}", username.toStdString());
            m_accountManager.add_microsoft_account(username, uuid, mcToken, msRefreshToken);
            
            update_status("Login successful!", false);
            QTimer::singleShot(1000, this, &QDialog::accept);
        });
    });
}

void MicrosoftLoginDialog::update_status(const QString& text, bool isError) {
    m_statusLabel->setText(text);
    if (isError) {
        m_statusLabel->setStyleSheet("color: #f38ba8; font-weight: bold;");
    } else {
        m_statusLabel->setStyleSheet("color: #a6e3a1; font-weight: bold;");
    }
}

void MicrosoftLoginDialog::on_cancel_clicked() {
    reject();
}

} // namespace flint::ui
