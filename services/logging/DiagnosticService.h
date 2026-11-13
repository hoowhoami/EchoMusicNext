#pragma once
#include "DiagnosticRecords.h"
#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QSortFilterProxyModel>
#include <QUrl>
#include <functional>
#include <memory>

namespace EchoMusic
{
struct DiagnosticExportResult
{
    QString error;
    bool cancelled = false;
    QStringList missing{};
};
/// Application-owned local diagnostics. Workers receive value snapshots only;
/// UI unload cancels its export, shutdown cancels and joins before the log sink.
class DiagnosticService final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *records READ records CONSTANT)
    Q_PROPERTY(QVariantMap latestRecord READ latestRecord NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QUrl exportedFile READ exportedFile NOTIFY changed)
public:
    using Flush = std::function<bool(const QString &, const std::atomic_bool &)>;
    explicit DiagnosticService(QString directory, Flush flush = {}, QObject *parent = nullptr);
    ~DiagnosticService() override;
    QAbstractItemModel *records()
    {
        return &m_crashRecords;
    }
    QVariantMap latestRecord() const;
    int count() const
    {
        return m_crashRecords.rowCount();
    }
    bool loading() const
    {
        return m_loading;
    }
    bool busy() const
    {
        return m_busy;
    }
    int progress() const
    {
        return m_progress;
    }
    QString status() const
    {
        return m_status;
    }
    QString errorString() const
    {
        return m_error;
    }
    QUrl exportedFile() const
    {
        return m_exported;
    }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap record(const QString &id) const;
    Q_INVOKABLE QString suggestedFileName(const QString &id = {}) const;
    Q_INVOKABLE void exportCurrent(const QUrl &destination, const QString &notes = {});
    Q_INVOKABLE void exportCrash(const QString &id, const QUrl &destination, bool includeDump,
                                 const QString &notes = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resetExport();
    Q_INVOKABLE void revealExport() const;
    Q_INVOKABLE void checkStartupNotice();
signals:
    void changed();
    void notice(const QString &message, bool failure);
    void startupNotice(const QString &message);
    void recordsRequested();

private:
    void startExport(DiagnosticRecord record, bool current, const QUrl &destination, bool dump,
                     const QString &notes);
    void publishStartupNotice();
    QString m_directory;
    Flush m_flush;
    QVector<DiagnosticRecord> m_records;
    // Unknown exits remain internal fallback context for the default export,
    // without becoming crash history entries or triggering crash notices.
    QSortFilterProxyModel m_crashRecords;
    QThreadPool m_pool;
    QFutureWatcher<DiagnosticScan> m_scan;
    QFutureWatcher<DiagnosticExportResult> m_export;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_loading = false, m_busy = false, m_loaded = false, m_startupRequested = false,
         m_startupChecked = false;
    int m_progress = 0;
    QString m_status, m_error;
    QUrl m_exported;
};
} // namespace EchoMusic
