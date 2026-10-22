#pragma once
#include <QObject>
#include <QKeySequence>
#include <memory>

namespace EchoMusic
{
/// Native, application-owned registrations. Replacing a set releases the old
/// native resources; destruction never leaves a shortcut installed in the OS.
class GlobalShortcuts final : public QObject
{
    Q_OBJECT
public:
    struct Binding
    {
        int command;
        QKeySequence sequence;
        QString description;
    };
    explicit GlobalShortcuts(QObject *parent = nullptr);
    ~GlobalShortcuts() override;
    void replace(const QList<Binding> &bindings);
signals:
    void registered(int command);
    void activated(int command);
    void registrationFailed(int command, const QString &reason);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace EchoMusic
