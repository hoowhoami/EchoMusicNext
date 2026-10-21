#pragma once
#include "LyricModel.h"
#include "NativeBridge.h"
#include <QSettings>
#include <QJsonObject>
namespace EchoMusic
{
/// Original domain lyrics remain immutable. Native conversion is contextual,
/// generation-bound and splits back by Unicode scalar counts, never UTF-16 units.
class LyricTextController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString conversion READ conversion WRITE setConversion NOTIFY changed)
    Q_PROPERTY(int globalOffsetMs READ globalOffsetMs WRITE setGlobalOffsetMs NOTIFY changed)
    Q_PROPERTY(int trackOffsetMs READ trackOffsetMs NOTIFY changed)
    Q_PROPERTY(double offsetStep READ offsetStep WRITE setOffsetStep NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool wantTranslation READ wantTranslation WRITE setWantTranslation NOTIFY changed)
    Q_PROPERTY(bool wantRomanization READ wantRomanization WRITE setWantRomanization NOTIFY changed)
    Q_PROPERTY(
        bool romanizationAsRuby READ romanizationAsRuby WRITE setRomanizationAsRuby NOTIFY changed)
    Q_PROPERTY(bool filterEnabled READ filterEnabled WRITE setFilterEnabled NOTIFY changed)
    Q_PROPERTY(QString filterPattern READ filterPattern WRITE setFilterPattern NOTIFY changed)
    Q_PROPERTY(QString filterError READ filterError NOTIFY changed)
public:
    bool romanizationAsRuby() const
    {
        return m_ruby;
    }
    bool filterEnabled() const
    {
        return m_filterEnabled;
    }
    QString filterPattern() const
    {
        return m_filterPattern;
    }
    QString filterError() const;
    void setRomanizationAsRuby(bool);
    void setFilterEnabled(bool);
    void setFilterPattern(const QString &);
    LyricTextController(LyricModel &model, NativeBridge &native, QSettings &settings,
                        QObject *parent = nullptr);
    QString conversion() const
    {
        return m_mode;
    }
    int globalOffsetMs() const
    {
        return m_globalOffset;
    }
    int trackOffsetMs() const
    {
        return m_trackOffset;
    }
    double offsetStep() const
    {
        return m_step;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    bool replace(const QJsonObject &lyrics, const QString &track);
    void clear();
    void setPosition(qint64 position);
    bool wantTranslation() const
    {
        return m_wantTranslation;
    }
    bool wantRomanization() const
    {
        return m_wantRomanization;
    }
    void setWantTranslation(bool value);
    void setWantRomanization(bool value);
    Q_INVOKABLE void setConversion(const QString &mode);
    Q_INVOKABLE void setGlobalOffsetMs(int offset);
    Q_INVOKABLE void setOffsetStep(double step);
    Q_INVOKABLE void adjustTrackOffset(int direction);
    Q_INVOKABLE void resetTrackOffset();
    Q_INVOKABLE void copyLyrics();
signals:
    void changed();
    void copied();
    void notice(const QString &message);

private:
    void updateFilter();
    void apply();
    void publish();
    void completed(quint64 id, quint64 revision, bool success, const QByteArray &bytes);
    LyricModel &m_model;
    NativeBridge &m_native;
    QSettings &m_settings;
    QJsonObject m_original, m_presented;
    QString m_mode, m_track, m_error;
    quint64 m_revision = 0, m_request = 0;
    qint64 m_position = 0;
    int m_globalOffset = 0, m_trackOffset = 0;
    double m_step = .1;
    bool m_ruby = false, m_filterEnabled = false;
    QString m_filterPattern;
    bool m_wantTranslation = false, m_wantRomanization = false;
};
} // namespace EchoMusic
