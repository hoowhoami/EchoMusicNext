// Copyright (C) 2016 basysKom GmbH.
// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only
// Adapted from qtdeclarative tst_qv4mm.cpp at upstream commits
// 7d5e24963a82dd6dc7419fc2f943042db2598bde and 9eca4322acef46f67405e891d9a61661a4fc26ee.
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtTest>
#include <private/qv4engine_p.h>
#include <private/qv4functionobject_p.h>
#include <private/qv4mm_p.h>
#include <private/qv4mmdefs_p.h>
#include <private/qv4object_p.h>
using namespace Qt::StringLiterals;
class Qv4GcTest : public QObject
{
    Q_OBJECT
private slots:
    void generatorFrameMarking_data();
    void generatorFrameMarking();
    void defaultDerivedConstructorDuringGc_data();
    void defaultDerivedConstructorDuringGc();
    void quickLoadsAgainstQml();
};
static QV4::Heap::Base *trackedGeneratorLocal = nullptr;

static QV4::ReturnedValue method_track_generator_local(const QV4::FunctionObject *,
                                                       const QV4::Value *, const QV4::Value *argv,
                                                       int argc)
{
    Q_ASSERT(argc == 1);
    trackedGeneratorLocal = argv[0].heapObject();
    return argv[0].asReturnedValue();
}

static void runGcUntil(QV4::GCStateMachine *sm, QV4::GCState target)
{
    while (sm->state != target) {
        QV4::GCStateInfo &stateInfo = sm->stateInfoMap[int(sm->state)];
        sm->state = stateInfo.execute(sm, sm->stateData);
    }
}

static QV4::ReturnedValue method_finish_gc(const QV4::FunctionObject *f, const QV4::Value *,
                                           const QV4::Value *, int)
{
    runGcUntil(f->engine()->memoryManager->gcStateMachine.get(), QV4::GCState::Invalid);
    return QV4::Encode::undefined();
}

void Qv4GcTest::generatorFrameMarking_data()
{
    QTest::addColumn<QString>("duringGc");

    // The generator stores a fresh string in a local and yields after the generator's
    // frame has already been marked.
    QTest::addRow("suspended") << u"suspended.next()"_s;

    // The gc completes while the generator is still executing.
    QTest::addRow("executing") << u"executing.next()"_s;
}

void Qv4GcTest::generatorFrameMarking()
{
    QFETCH(QString, duringGc);

    QJSEngine jsEngine;
    QV4::ExecutionEngine *v4 = jsEngine.handle();
    v4->globalObject->defineDefaultProperty(u"__track"_s, method_track_generator_local);
    v4->globalObject->defineDefaultProperty(u"__finishGc"_s, method_finish_gc);

    jsEngine.evaluate(uR"(
        function* suspendedGenerator() {
            for (;;) {
                const s = __track("x".repeat(64) + Math.random());
                yield;
                s.length;
            }
        }
        var suspended = suspendedGenerator();
        suspended.next();

        function* executingGenerator() {
            const s = __track("x".repeat(64) + Math.random());
            __finishGc();
            yield;
            s.length;
        }
        var executing = executingGenerator();
    )"_s);

    auto *sm = v4->memoryManager->gcStateMachine.get();
    sm->reset();
    v4->memoryManager->gcBlocked = QV4::MemoryManager::Unblocked;
    runGcUntil(sm, QV4::GCState::MarkReady);

    trackedGeneratorLocal = nullptr;
    jsEngine.evaluate(duringGc);
    QVERIFY(trackedGeneratorLocal);

    runGcUntil(sm, QV4::GCState::Invalid);
    QVERIFY(trackedGeneratorLocal->inUse());
}

void Qv4GcTest::defaultDerivedConstructorDuringGc_data()
{
    QTest::addColumn<QString>("source");
    QTest::addRow("base finishes gc") << uR"(
        class Base { constructor() { __finishGc(); } }
        class Derived extends Base {}
        new Derived();
    )"_s;
    QTest::addRow("two levels") << uR"(
        class Base { constructor() { __finishGc(); } }
        class Middle extends Base {}
        class Derived extends Middle {}
        new Derived();
    )"_s;
    QTest::addRow("inside generator") << uR"(
        class Base { constructor() { __finishGc(); } }
        class Derived extends Base {}
        function* g() { const d = new Derived(); yield d; }
        g().next();
    )"_s;
}

void Qv4GcTest::defaultDerivedConstructorDuringGc()
{
    QFETCH(QString, source);

    QJSEngine jsEngine;
    QV4::ExecutionEngine *v4 = jsEngine.handle();
    v4->globalObject->defineDefaultProperty(u"__finishGc"_s, method_finish_gc);

    auto *sm = v4->memoryManager->gcStateMachine.get();
    sm->reset();
    v4->memoryManager->gcBlocked = QV4::MemoryManager::Unblocked;
    runGcUntil(sm, QV4::GCState::MarkReady);

    const QJSValue result = jsEngine.evaluate(source);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
}

void Qv4GcTest::quickLoadsAgainstQml()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nItem { width: 31; height: 47 }", QUrl());
    std::unique_ptr<QObject> item(component.create());
    QVERIFY2(item, qPrintable(component.errorString()));
    auto *quick = qobject_cast<QQuickItem *>(item.get());
    QVERIFY(quick);
    QCOMPARE(quick->width(), 31.0);
}
QTEST_MAIN(Qv4GcTest)
#include "Qv4GcTest.moc"
