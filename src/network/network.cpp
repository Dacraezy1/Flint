#include "network.hpp"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QFileInfo>

namespace flint::network {

QByteArray get(const QString& url, const QMap<QString, QString>& headers, bool* success) {
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    
    // Set custom headers
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
    
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    QByteArray data;
    bool ok = false;
    if (reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
        ok = true;
    }
    if (success) {
        *success = ok;
    }
    reply->deleteLater();
    return data;
}

QByteArray post(const QString& url, const QByteArray& payload, 
                const QMap<QString, QString>& headers, bool* success) {
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    
    // Set custom headers
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
    
    // Default content type to JSON if not specified
    if (!headers.contains("Content-Type")) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    }
    
    QNetworkReply* reply = manager.post(request, payload);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    QByteArray data = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    if (success) {
        *success = ok;
    }
    reply->deleteLater();
    return data;
}

bool download_file(const QString& url, const QString& filePath, 
                   std::function<void(qint64 bytesReceived, qint64 bytesTotal)> progressCallback) {
    QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.absolutePath());
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    
    QNetworkReply* reply = manager.get(request);
    
    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        file.write(reply->readAll());
    });
    
    if (progressCallback) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, progressCallback);
    }
    
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    bool ok = (reply->error() == QNetworkReply::NoError);
    if (ok) {
        file.write(reply->readAll());
    }
    
    file.close();
    reply->deleteLater();
    
    if (!ok) {
        file.remove();
    }
    return ok;
}

} // namespace flint::network
