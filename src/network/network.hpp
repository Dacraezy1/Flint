#pragma once

#include <QString>
#include <QByteArray>
#include <functional>

#include <QMap>

namespace flint::network {

// Fetch contents of a URL as a byte array (synchronous, blocks using QEventLoop)
QByteArray get(const QString& url, const QMap<QString, QString>& headers = {}, bool* success = nullptr);

// Send a POST request (synchronous, blocks using QEventLoop)
QByteArray post(const QString& url, const QByteArray& payload, 
                const QMap<QString, QString>& headers = {}, bool* success = nullptr);

// Download a file from URL to local file path (synchronous, blocks using QEventLoop)
bool download_file(const QString& url, const QString& filePath, 
                   std::function<void(qint64 bytesReceived, qint64 bytesTotal)> progressCallback = nullptr);

} // namespace flint::network
