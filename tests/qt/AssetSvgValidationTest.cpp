#include "AssetSvgValidation.h"
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

using namespace EchoMusic;

class AssetSvgValidationTest final : public QObject
{
    Q_OBJECT
private slots:
    void staticCssGradientClipMaskAndEmbeddedRaster()
    {
        QImage raster(1, 1, QImage::Format_RGBA8888);
        raster.fill(Qt::green);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(raster.save(&buffer, "PNG"));
        const auto source =
            QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='32' height='16'>"
                       "<style>.paint{fill:url(#g)}</style><defs>"
                       "<linearGradient id='g'><stop stop-color='red'/><stop offset='1' "
                       "stop-color='blue'/>"
                       "</linearGradient><clipPath id='c'><rect width='16' height='16'/></clipPath>"
                       "<mask id='m'><rect width='32' height='16' fill='white'/></mask></defs>"
                       "<rect width='32' height='16' class='paint' style='fill:url(#g);opacity:1' "
                       "clip-path='url(#c)' mask='url(#m)'/>"
                       "<image x='16' width='16' height='16' href='data:image/png;base64,") +
            png.toBase64() + "'/></svg>";
        QImage image;
        QString error;
        QVERIFY2(AssetSvgValidation::render(source, image, error), qPrintable(error));
        QCOMPARE(image.size(), QSize(32, 16));
        QVERIFY(image.pixelColor(4, 8).red() > image.pixelColor(4, 8).blue());
        QVERIFY(image.pixelColor(24, 8).green() > 200);
    }
    void forbiddenReferences_data()
    {
        QTest::addColumn<QByteArray>("source");
        const QByteArray prefix("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>");
        for (const auto &reference :
             {"file:///etc/hosts", "/tmp/external.png", "../external.png",
              "https://example.invalid/image.png", "data:image/svg+xml;base64,PHN2Zy8+"})
            QTest::newRow(reference)
                << prefix + "<image width='16' height='16' href='" + reference + "'/></svg>";
        QTest::newRow("filter-intermediates")
            << prefix + "<defs><filter id='f'><feFlood result='a'/><feFlood "
                        "result='b'/></filter></defs><rect width='16' height='16' "
                        "filter='url(#f)'/></svg>";
        QTest::newRow("nested-group-opacity")
            << prefix + "<g opacity='.5'><rect width='16' height='16'/><g opacity='.5'><rect "
                        "width='16' height='16'/></g></g></svg>";
        QTest::newRow("css-group-opacity")
            << prefix + "<style>.fade{opacity:.5}</style><g class='fade'><rect width='16' "
                        "height='16'/><rect width='8' height='8'/></g></svg>";
        QTest::newRow("multiple-mask-surfaces")
            << prefix + "<defs><mask id='m'><rect width='16' height='16' "
                        "fill='white'/></mask></defs><g mask='url(#m)'><rect width='16' "
                        "height='16'/><rect width='16' height='16' mask='url(#m)'/></g></svg>";
        QTest::newRow("css-mask-selector")
            << prefix + "<style>.m{mask:url(#m)}</style><defs><mask id='m'><rect width='16' "
                        "height='16'/></mask></defs><g class='m'><rect width='16' "
                        "height='16'/></g></svg>";
        QTest::newRow("pattern-recursion")
            << prefix + "<defs><pattern id='p' width='1' height='1'><rect width='1' "
                        "height='1'/></pattern></defs><rect width='16' height='16' "
                        "fill='url(#p)'/></svg>";
        QTest::newRow("use-expansion") << prefix + "<defs><g id='a'><rect width='16' "
                                                   "height='16'/></g></defs><use href='#a'/></svg>";
        QTest::newRow("gradient-inheritance")
            << prefix + "<defs><linearGradient id='a'><stop "
                        "stop-color='red'/></linearGradient><linearGradient id='b' "
                        "href='#a'/></defs><rect width='16' height='16' fill='url(#b)'/></svg>";
        QTest::newRow("leaf-marker")
            << prefix +
                   "<g id='g'/><path opacity='.5' marker-start='url(#g)' d='M0 0 L10 10'/></svg>";
        QTest::newRow("css-marker") << prefix + "<style>.x{marker-start:url(#g)}</style><g "
                                                "id='g'/><path class='x' d='M0 0 L10 10'/></svg>";
        QTest::newRow("wrong-mask-type")
            << prefix + "<g id='g'/><rect width='16' height='16' mask='url(#g)'/></svg>";
        QTest::newRow("wrong-clip-type")
            << prefix + "<g id='g'/><rect width='16' height='16' clip-path='url(#g)'/></svg>";
        QTest::newRow("wrong-fill-type")
            << prefix + "<g id='g'/><rect width='16' height='16' fill='url(#g)'/></svg>";
        QTest::newRow("wrong-css-stroke-type")
            << prefix + "<style>.x{stroke:url(#g)}</style><g id='g'/><path class='x' d='M0 0 L10 "
                        "10'/></svg>";
        QTest::newRow("unknown-url-property")
            << prefix + "<style>.x{color:url(#g)}</style><g id='g'/><rect class='x' width='16' "
                        "height='16'/></svg>";
        QTest::newRow("dtd") << QByteArray("<!DOCTYPE svg [<!ENTITY x 'foo'>]>") + prefix +
                                    "</svg>";
        QTest::newRow("stylesheet-pi")
            << QByteArray("<?xml-stylesheet type='text/css' href='/tmp/style.css'?>") + prefix +
                   "</svg>";
        QTest::newRow("escaped-css")
            << prefix + "<style>.x{fill:u\\72l(file:///etc/hosts)}</style></svg>";
        QTest::newRow("css-import") << prefix + "<style>@import '/tmp/style.css';</style></svg>";
        QTest::newRow("style-child")
            << prefix +
                   "<style>.a{fill:red}<g/>.a{fill:url(file:///tmp/external.png)}</style></svg>";
        QTest::newRow("split-cdata-url")
            << prefix + "<style>.a{fill:u<![CDATA[rl]]>(file:///tmp/external.png)}</style></svg>";
        QTest::newRow("foreign-object") << prefix + "<foreignObject/></svg>";
        QTest::newRow("event") << prefix + "<rect onload='alert(1)'/></svg>";
        QTest::newRow("non-finite") << QByteArray("<svg width='NaN' height='16'/>");
        QTest::newRow("oversize") << QByteArray("<svg width='4097' height='16'/>");
    }
    void forbiddenReferences()
    {
        QFETCH(QByteArray, source);
        QImage image;
        QString error;
        QVERIFY(!AssetSvgValidation::render(source, image, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(image.isNull());
    }
};
QTEST_MAIN(AssetSvgValidationTest)
#include "AssetSvgValidationTest.moc"
