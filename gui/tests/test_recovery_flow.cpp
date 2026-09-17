/*
 * The unsaved-work snapshot, end to end through a real EditorViewport:
 * written while dirty, dropped on save, found and restored on the next
 * open. See docs/adr/0110.
 *
 * What it cannot do is kill the process. A snapshot survives precisely
 * because a crash never runs the destructor, and the destructor is what
 * removes it — so the test drives the pieces either side of that and
 * leaves the SIGKILL itself to the operating system.
 */
#include "editor_viewport.h"

#include "ase/buffer.h"
#include "ase/recovery.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QTest>

#include <cstdlib>

namespace {

QString configDir() {
    return QDir(qEnvironmentVariable("XDG_CONFIG_HOME")).filePath(QStringLiteral("ase"));
}

QString recoveryDir() {
    return QDir(configDir()).filePath(QStringLiteral("recovery"));
}

AseBuffer *bufferFrom(const QByteArray &text) {
    AseBuffer *buffer = ase_buffer_create();
    if (!text.isEmpty()) {
        ase_buffer_insert(buffer, 0, text.constData(), static_cast<size_t>(text.size()));
    }
    return buffer;
}

void type(EditorViewport &viewport, const QString &text) {
    for (const QChar &c : text) {
        QKeyEvent press(QEvent::KeyPress, c.unicode(), Qt::NoModifier, QString(c));
        QApplication::sendEvent(&viewport, &press);
    }
}

} // namespace

class RecoveryFlow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_file = QDir(QDir::tempPath()).filePath(QStringLiteral("ase_recovery_flow.txt"));
        QDir().mkpath(configDir());
    }

    void init() {
        QFile::remove(m_file);
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("on disk\n");
        f.close();
        ase_recovery_remove(recoveryDir().toUtf8().constData(), m_file.toUtf8().constData());
    }

    /* Nothing unsaved, nothing kept: a snapshot for a clean buffer would
     * prompt on the next open for no reason. */
    void cleanBufferKeepsNoSnapshot() {
        EditorViewport viewport(bufferFrom("on disk\n"), m_file);
        QVERIFY(!viewport.hasRecoverySnapshot());
        QTest::qWait(kPastTheTimer);
        QVERIFY(!viewport.hasRecoverySnapshot());
    }

    void editedBufferIsSnapshotted() {
        AseBuffer *buffer = bufferFrom("on disk\n");
        EditorViewport viewport(buffer, m_file);
        type(viewport, QStringLiteral("iedited"));
        QVERIFY(!viewport.hasRecoverySnapshot()); /* not yet — it waits for a pause */
        QTest::qWait(kPastTheTimer);
        QVERIFY(viewport.hasRecoverySnapshot());

        size_t len = 0;
        char *content = ase_recovery_read(recoveryDir().toUtf8().constData(),
                                           m_file.toUtf8().constData(), &len);
        QVERIFY(content != nullptr);
        QCOMPARE(QString::fromUtf8(content, static_cast<int>(len)), QStringLiteral("editedon disk\n"));
        free(content);

        /* The file itself is untouched until a save. */
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("on disk\n"));
    }

    /* Saving makes the file the work, so there is nothing left to
     * recover and the next open must not ask. */
    void savingDropsTheSnapshot() {
        EditorViewport viewport(bufferFrom("on disk\n"), m_file);
        type(viewport, QStringLiteral("iedited"));
        QTest::qWait(kPastTheTimer);
        QVERIFY(viewport.hasRecoverySnapshot());

        viewport.runCommand(QStringLiteral("w"));
        QVERIFY(!viewport.hasRecoverySnapshot());
    }

    /* Undoing back to what is on disk is no longer unsaved work. */
    void returningToTheSavedStateDropsTheSnapshot() {
        EditorViewport viewport(bufferFrom("on disk\n"), m_file);
        type(viewport, QStringLiteral("iedited"));
        QTest::qWait(kPastTheTimer);
        QVERIFY(viewport.hasRecoverySnapshot());

        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&viewport, &esc);
        type(viewport, QStringLiteral("u")); /* until the buffer matches the file again */
        type(viewport, QStringLiteral("u"));
        QTest::qWait(kPastTheTimer);
        QVERIFY(!viewport.hasRecoverySnapshot());
    }

    /* The next session: a snapshot left by a crash is found and loaded. */
    void aSnapshotIsFoundAndRestored() {
        const char *recovered = "work that never reached the disk\n";
        QVERIFY(ase_recovery_write(recoveryDir().toUtf8().constData(),
                                    m_file.toUtf8().constData(), recovered,
                                    strlen(recovered)));

        AseBuffer *buffer = bufferFrom("on disk\n");
        EditorViewport viewport(buffer, m_file);
        QVERIFY(viewport.hasRecoverySnapshot());
        QVERIFY(viewport.restoreFromRecovery());

        size_t len = ase_buffer_length(buffer);
        QByteArray actual(static_cast<int>(len), '\0');
        ase_buffer_get_text(buffer, 0, len, actual.data());
        QCOMPARE(actual, QByteArray(recovered));

        /* Restoring is an edit, so it is reversible — the user is not
         * stuck with the recovered version. */
        type(viewport, QStringLiteral("u"));
        len = ase_buffer_length(buffer);
        QByteArray afterUndo(static_cast<int>(len), '\0');
        ase_buffer_get_text(buffer, 0, len, afterUndo.data());
        QCOMPARE(afterUndo, QByteArray("on disk\n"));
    }

    /* Choosing to discard means the next open does not ask again. */
    void discardingRemovesIt() {
        const char *recovered = "unwanted\n";
        QVERIFY(ase_recovery_write(recoveryDir().toUtf8().constData(),
                                    m_file.toUtf8().constData(), recovered, strlen(recovered)));
        EditorViewport viewport(bufferFrom("on disk\n"), m_file);
        QVERIFY(viewport.hasRecoverySnapshot());
        viewport.discardRecovery();
        QVERIFY(!viewport.hasRecoverySnapshot());
    }

    /* Closing normally is not a crash: the work was saved or dropped on
     * purpose, so nothing should be left to prompt about next time. */
    void closingCleanlyRemovesIt() {
        {
            EditorViewport viewport(bufferFrom("on disk\n"), m_file);
            type(viewport, QStringLiteral("iedited"));
            QTest::qWait(kPastTheTimer);
            QVERIFY(viewport.hasRecoverySnapshot());
        }
        QVERIFY(!ase_recovery_exists(recoveryDir().toUtf8().constData(),
                                      m_file.toUtf8().constData()));
    }

    /* The gap this suite did not cover: a buffer with no filename got
     * no snapshot at all, and it is the one with nothing on disk to
     * fall back to. See docs/adr/0127. */
    void untitledBufferIsSnapshotted() {
        EditorViewport viewport(ase_buffer_create(), QString());
        QVERIFY(viewport.isUntitled());
        type(viewport, QStringLiteral("ithoughts with no file"));
        QTest::qWait(kPastTheTimer);
        QVERIFY(viewport.hasRecoverySnapshot());

        const QString key = viewport.recoveryKey();
        QVERIFY(key.startsWith(QStringLiteral("untitled:")));

        size_t len = 0;
        char *content = ase_recovery_read(recoveryDir().toUtf8().constData(),
                                           key.toUtf8().constData(), &len);
        QVERIFY(content != nullptr);
        QCOMPARE(QByteArray(content, static_cast<int>(len)), QByteArray("thoughts with no file"));
        free(content);
        /* The destructor drops it, as it does for a named buffer. */
    }

    /* Two untitled buffers in one window must not share a snapshot, or
     * the second silently overwrites the first. */
    void twoUntitledBuffersGetTheirOwn() {
        EditorViewport first(ase_buffer_create(), QString());
        EditorViewport second(ase_buffer_create(), QString());
        type(first, QStringLiteral("ifirst"));
        type(second, QStringLiteral("isecond"));
        QTest::qWait(kPastTheTimer);

        QVERIFY(first.recoveryKey() != second.recoveryKey());

        size_t len = 0;
        char *content = ase_recovery_read(recoveryDir().toUtf8().constData(),
                                           first.recoveryKey().toUtf8().constData(), &len);
        QVERIFY(content != nullptr);
        QCOMPARE(QByteArray(content, static_cast<int>(len)), QByteArray("first"));
        free(content);
    }

    /* Saving moves the buffer to a new key. The old snapshot has to go,
     * or it is offered back at the next start as work that was lost. */
    void savingAnUntitledBufferDropsItsUntitledSnapshot() {
        QString target = QDir(QDir::tempPath()).filePath(QStringLiteral("ase_recovery_named.txt"));
        QFile::remove(target);

        EditorViewport viewport(ase_buffer_create(), QString());
        type(viewport, QStringLiteral("iwork"));
        QTest::qWait(kPastTheTimer);
        QString untitled = viewport.recoveryKey();
        QVERIFY(ase_recovery_exists(recoveryDir().toUtf8().constData(),
                                     untitled.toUtf8().constData()));

        viewport.saveAs(target);
        QVERIFY(!ase_recovery_exists(recoveryDir().toUtf8().constData(),
                                      untitled.toUtf8().constData()));
        QCOMPARE(viewport.recoveryKey(), target);
        QVERIFY(!viewport.hasRecoverySnapshot()); /* saved, so nothing unsaved */

        QFile::remove(target);
    }

    void cleanupTestCase() { QFile::remove(m_file); }

private:
    /* The viewport waits kRecoveryDelayMs for typing to stop. */
    static constexpr int kPastTheTimer = 1400;
    QString m_file;
};

QTEST_MAIN(RecoveryFlow)
#include "test_recovery_flow.moc"
