#include "ApplicationTranslations.h"
#include <QCoreApplication>

namespace EchoMusic
{
ApplicationTranslations::~ApplicationTranslations()
{
    if (m_translator && QCoreApplication::instance())
        QCoreApplication::removeTranslator(m_translator.get());
}
bool ApplicationTranslations::load(const QLocale &locale, const QString &directory,
                                   const QString &base)
{
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(locale, base, QStringLiteral("_"), directory))
        return false;
    if (m_translator)
        QCoreApplication::removeTranslator(m_translator.get());
    if (!QCoreApplication::installTranslator(translator.get()))
        return false;
    m_translator = std::move(translator);
    return true;
}
} // namespace EchoMusic
