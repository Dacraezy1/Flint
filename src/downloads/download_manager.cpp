#include "download_manager.hpp"
#include "network/network.hpp"
#include "logging/logging.hpp"
#include <QFile>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QRunnable>

namespace flint::downloads {

class DownloadRunnable : public QRunnable {
public:
    DownloadRunnable(const DownloadTask& task, 
                     std::atomic<int>& completedCount, 
                     int totalCount, 
                     std::atomic<bool>& cancelled, 
                     std::atomic<bool>& failed, 
                     DownloadManager* manager)
        : m_task(task), 
          m_completedCount(completedCount), 
          m_totalCount(totalCount),
          m_cancelled(cancelled), 
          m_failed(failed), 
          m_manager(manager) {
        setAutoDelete(true);
    }

    void run() override {
        if (m_cancelled.load()) return;
        
        bool needsDownload = true;
        QFileInfo fileInfo(m_task.path);
        
        if (fileInfo.exists()) {
            bool sizeMatch = (m_task.size <= 0) || (fileInfo.size() == m_task.size);
            if (sizeMatch && DownloadManager::verify_sha1(m_task.path, m_task.sha1)) {
                needsDownload = false;
            }
        }
        
        bool success = true;
        if (needsDownload) {
            flint::logging::debug("Downloading {} to {}", m_task.url.toStdString(), m_task.path.toStdString());
            success = flint::network::download_file(m_task.url, m_task.path);
            if (success && !m_task.sha1.isEmpty()) {
                success = DownloadManager::verify_sha1(m_task.path, m_task.sha1);
                if (!success) {
                    flint::logging::error("SHA1 verification failed for {}", m_task.path.toStdString());
                    QFile::remove(m_task.path);
                }
            }
        }
        
        if (!success && !m_cancelled.load()) {
            m_failed.store(true);
        }
        
        int done = ++m_completedCount;
        if (!m_cancelled.load()) {
            int pct = (done * 100) / m_totalCount;
            emit m_manager->task_completed(done, m_totalCount, m_task.url, success);
            emit m_manager->progress(pct, QString("Downloading: %1/%2").arg(done).arg(m_totalCount));
            
            if (done == m_totalCount) {
                emit m_manager->finished(!m_failed.load());
            }
        }
    }
    
private:
    DownloadTask m_task;
    std::atomic<int>& m_completedCount;
    int m_totalCount;
    std::atomic<bool>& m_cancelled;
    std::atomic<bool>& m_failed;
    DownloadManager* m_manager;
};

DownloadManager::DownloadManager(QObject* parent) 
    : QObject(parent) {
    m_threadPool.setMaxThreadCount(16);
}

DownloadManager::~DownloadManager() {
    cancel();
}

bool DownloadManager::verify_sha1(const QString& filePath, const QString& expectedSha1) {
    if (expectedSha1.isEmpty()) {
        return true;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (hash.addData(&file)) {
        return hash.result().toHex().toLower() == expectedSha1.toLower();
    }
    return false;
}

void DownloadManager::start_downloads(const QVector<DownloadTask>& tasks, int maxConcurrent) {
    cancel();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelledState.store(false);
    m_failedState.store(false);
    m_completedCount.store(0);
    m_threadPool.setMaxThreadCount(maxConcurrent);
    
    if (tasks.isEmpty()) {
        emit progress(100, "No files to download");
        emit finished(true);
        return;
    }
    
    int totalCount = tasks.size();
    for (const auto& task : tasks) {
        auto* runnable = new DownloadRunnable(task, m_completedCount, totalCount, m_cancelledState, m_failedState, this);
        m_threadPool.start(runnable);
    }
}

void DownloadManager::cancel() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelledState.store(true);
    m_threadPool.clear();
    m_threadPool.waitForDone();
}

} // namespace flint::downloads
