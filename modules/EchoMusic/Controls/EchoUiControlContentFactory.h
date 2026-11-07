#pragma once
#include "EchoUiContentFactory.h"
namespace EchoMusic
{
// Adapts the native render slots, retaining their QObject ownership, bindings,
// control actions and live editor state. Created only for an actual composition.
class EchoUiControlContentFactory final : public EchoUiContentFactory
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool covered READ covered NOTIFY coverageChanged)
    Q_PROPERTY(QVariantList renderSlots READ renderSlots NOTIFY slotsChanged)
public:
    EchoUiControlContentFactory(QQuickItem *, QQmlEngine &, QObject *);
    ~EchoUiControlContentFactory() override;
    static bool supports(QQuickItem *);
    bool covered() const
    {
        return m_covered || m_legacyCovered;
    }
    void setLegacyCovered(bool value)
    {
        if (m_legacyCovered != value) {
            m_legacyCovered = value;
            emit coverageChanged();
        }
    }
    QVariantList renderSlots() const;
    QQuickItem *create(QQuickItem *, EchoUiContentFactory * = nullptr) override;
    void present(EchoUiContentFactory *);
    void restore(QQuickItem *);
signals:
    void coverageChanged();
    void slotsChanged();
private slots:
    void refreshSlots();

private:
    struct Slot
    {
        QPointer<QQuickItem> item, origin, next;
    };
    QList<Slot> m_slots;
    QPointer<QQuickItem> m_target;
    QPointer<EchoUiContentHost> m_host;
    QPointer<QObject> m_visibility;
    QQmlEngine &m_engine;
    bool m_covered = false, m_legacyCovered = false;
};
} // namespace EchoMusic
