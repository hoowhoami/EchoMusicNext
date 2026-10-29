#pragma once
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QWindow>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
class QFileDialog;
namespace EchoMusic
{
/// Nonblocking native picker with a QWidget fallback. The QML owner controls
/// cancellation; no nested exec loop can outlive its page or account scope.
class EchoFileDialog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString title MEMBER m_title)
    Q_PROPERTY(QStringList nameFilters MEMBER m_nameFilters)
    Q_PROPERTY(QString defaultSuffix MEMBER m_defaultSuffix)
    Q_PROPERTY(QUrl currentFolder MEMBER m_currentFolder)
    Q_PROPERTY(QWindow *parentWindow MEMBER m_parentWindow)
    Q_PROPERTY(FileMode fileMode MEMBER m_fileMode)
    Q_PROPERTY(int options MEMBER m_options)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    Q_PROPERTY(QList<QUrl> selectedFiles READ selectedFiles NOTIFY selectionChanged)
    Q_PROPERTY(QUrl selectedFile READ selectedFile NOTIFY selectionChanged)
    Q_PROPERTY(QUrl selectedFolder READ selectedFile NOTIFY selectionChanged)
public:
    enum FileMode
    {
        OpenFile,
        OpenFiles,
        SaveFile,
        OpenFolder
    };
    Q_ENUM(FileMode)
    // Matches QFileDialog's public option, without depending on Quick's helper.
    enum Option
    {
        DontUseNativeDialog = 0x08
    };
    Q_ENUM(Option)
    explicit EchoFileDialog(QObject *parent = nullptr);
    ~EchoFileDialog() override;
    bool visible() const;
    QList<QUrl> selectedFiles() const
    {
        return m_selected;
    }
    QUrl selectedFile() const
    {
        return m_selected.value(0);
    }
    Q_INVOKABLE void open();
    Q_INVOKABLE void close();
signals:
    void visibleChanged();
    void selectionChanged();
    void accepted();
    void rejected();

private:
    QString m_title, m_defaultSuffix;
    QStringList m_nameFilters;
    QUrl m_currentFolder;
    QPointer<QWindow> m_parentWindow;
    QPointer<QFileDialog> m_dialog;
    QList<QUrl> m_selected;
    FileMode m_fileMode = OpenFile;
    int m_options = 0;
};
} // namespace EchoMusic
