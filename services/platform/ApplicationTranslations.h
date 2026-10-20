#pragma once
#include <QLocale>
#include <QString>
#include <QTranslator>
#include <memory>

namespace EchoMusic
{
/// Owns the installed application catalog for the lifetime of the GUI session.
/// Only bundled catalogs are selected in production; absent locales retain the
/// Chinese source strings. Loading a catalog does not imply language coverage.
class ApplicationTranslations final
{
public:
    ~ApplicationTranslations();
    bool load(const QLocale &locale, const QString &directory = QStringLiteral(":/i18n"),
              const QString &base = QStringLiteral("echomusicnext"));

private:
    std::unique_ptr<QTranslator> m_translator;
};
} // namespace EchoMusic
