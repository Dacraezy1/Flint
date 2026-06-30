#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QThreadPool>
#include <atomic>
#include <mutex>

namespace flint::downloads {

struct DownloadTask {
    QString url;
    QString path;
    QString sha1;
    qint64 size = 0;
};

class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr);
    ~DownloadManager();

    // Start downloading a batch of tasks in parallel
    void start_downloads(const QVector<DownloadTask>& tasks, int maxConcurrent = 16);

    // Cancel all running downloads
    void cancel();

    // Helper to verify SHA1 checksum of a file
    static bool verify_sha1(const QString& filePath, const QString& expectedSha1);

signals:
    // Emitted when a single task completes
    void task_completed(int completedCount, int totalCount, const QString& url, bool success);
    
    // Emitted to show overall progress (from 0 to 100)
    void progress(int percentage, const QString& statusText);

    // Emitted when the entire batch is finished
    void finished(bool success);

private:
    QThreadPool m_threadPool;
    std::mutex m_mutex;

    // Batch download state
    std::atomic<int> m_completedCount{0};
    std::atomic<bool> m_cancelledState{false};
    std::atomic<bool> m_failedState{false};
};

} // namespace flint::downloads
