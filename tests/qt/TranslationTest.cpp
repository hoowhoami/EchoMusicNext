#include "ApplicationTranslations.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

using namespace EchoMusic;
class TranslationTest final : public QObject
{
    Q_OBJECT
private slots:
    void bundledCatalogTranslatesCppAndQmlAndReleasesOnDestruction()
    {
        QCOMPARE(QCoreApplication::translate("I18nFixture", "Hello"), QString("Hello"));
        {
            ApplicationTranslations translations;
            QVERIFY(translations.load(QLocale("en_US"), ":/test-i18n", "fixture"));
            QCOMPARE(QCoreApplication::translate("I18nFixture", "Hello"),
                     QString::fromUtf8("验证译文"));
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData("import QtQml\nQtObject { property string label: qsTr(\"Hello\") }",
                              QUrl("qrc:/I18nFixture.qml"));
            QScopedPointer<QObject> object(component.create());
            QVERIFY2(object, qPrintable(component.errorString()));
            QCOMPARE(object->property("label").toString(), QString::fromUtf8("验证译文"));
            QVERIFY(!translations.load(QLocale("fr_FR"), ":/test-i18n", "fixture"));
            QCOMPARE(QCoreApplication::translate("I18nFixture", "Hello"),
                     QString::fromUtf8("验证译文"));
        }
        QCOMPARE(QCoreApplication::translate("I18nFixture", "Hello"), QString("Hello"));
    }
};
QTEST_GUILESS_MAIN(TranslationTest)
#include "TranslationTest.moc"
