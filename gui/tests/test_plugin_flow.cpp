/*
 * A plugin command through a real EditorViewport: it edits the buffer
 * directly, underneath the undo stack, so the question is what the undo
 * stack holds afterwards. It used to hold nothing — the history was
 * thrown away on every plugin run. See docs/adr/0128.
 */
#include "editor_viewport.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QTest>

namespace {

QString pluginDir() {
    return QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
        .filePath(QStringLiteral("ase/plugins"));
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

QByteArray textOf(AseBuffer *buffer) {
    size_t length = ase_buffer_length(buffer);
    QByteArray out(static_cast<int>(length), Qt::Uninitialized);
    if (length > 0) {
        ase_buffer_get_text(buffer, 0, length, out.data());
    }
    return out;
}

/* The Vim layer reads event->text(), so a key sent without one is not
 * the key you meant. */
void press(EditorViewport &viewport, int key, const QString &text = QString()) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(&viewport, &event);
}

} // namespace

class PluginFlow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(QDir().mkpath(pluginDir()));
        QFile plugin(QDir(pluginDir()).filePath(QStringLiteral("shout.lua")));
        QVERIFY(plugin.open(QIODevice::WriteOnly | QIODevice::Text));
        plugin.write(
            "ase.register_command(\"shout\", function(buf)\n"
            "    local len = ase.buffer_length(buf)\n"
            "    local text = ase.buffer_get_text(buf, 0, len)\n"
            "    ase.buffer_delete(buf, 0, len)\n"
            "    ase.buffer_insert(buf, 0, string.upper(text))\n"
            "end)\n");
        plugin.close();
    }

    /* The whole point of loading the directory. */
    void aPluginCommandRuns() {
        AseBuffer *buffer = bufferFrom("quiet\n");
        EditorViewport viewport(buffer, QString());
        QVERIFY(viewport.runCommandByName(QStringLiteral("shout")));
        QCOMPARE(textOf(buffer), QByteArray("QUIET\n"));
    }

    void anUnknownNameIsNotClaimed() {
        EditorViewport viewport(bufferFrom("x\n"), QString());
        QVERIFY(!viewport.runCommandByName(QStringLiteral("no.such.command")));
    }

    /* One step: `u` takes back the command, not half of what it did. */
    void undoTakesBackThePluginCommand() {
        AseBuffer *buffer = bufferFrom("quiet\n");
        EditorViewport viewport(buffer, QString());
        QVERIFY(viewport.runCommandByName(QStringLiteral("shout")));
        QCOMPARE(textOf(buffer), QByteArray("QUIET\n"));

        press(viewport, Qt::Key_U, QStringLiteral("u"));
        QCOMPARE(textOf(buffer), QByteArray("quiet\n"));
    }

    /* The history before the command has to survive it. Throwing it away
     * meant running a formatter cost every step back to the start of the
     * session, silently. */
    void historyBeforeTheCommandSurvives() {
        AseBuffer *buffer = bufferFrom("");
        EditorViewport viewport(buffer, QString());
        type(viewport, QStringLiteral("ihello"));
        press(viewport, Qt::Key_Escape);
        QCOMPARE(textOf(buffer), QByteArray("hello"));

        QVERIFY(viewport.runCommandByName(QStringLiteral("shout")));
        QCOMPARE(textOf(buffer), QByteArray("HELLO"));

        press(viewport, Qt::Key_U, QStringLiteral("u")); /* the plugin command */
        QCOMPARE(textOf(buffer), QByteArray("hello"));

        press(viewport, Qt::Key_U, QStringLiteral("u")); /* the typing before it */
        QCOMPARE(textOf(buffer), QByteArray(""));
    }

    /* A command that changes nothing must not land an empty step for
     * `u` to spend itself on. */
    void aCommandThatChangesNothingLeavesNoStep() {
        AseBuffer *buffer = bufferFrom("SHOUTED\n");
        EditorViewport viewport(buffer, QString());
        type(viewport, QStringLiteral("ix"));
        press(viewport, Qt::Key_Escape);
        QCOMPARE(textOf(buffer), QByteArray("xSHOUTED\n"));

        QVERIFY(viewport.runCommandByName(QStringLiteral("shout")));
        QCOMPARE(textOf(buffer), QByteArray("XSHOUTED\n"));

        press(viewport, Qt::Key_U, QStringLiteral("u"));
        QCOMPARE(textOf(buffer), QByteArray("xSHOUTED\n"));
        press(viewport, Qt::Key_U, QStringLiteral("u"));
        QCOMPARE(textOf(buffer), QByteArray("SHOUTED\n"));
    }

    void cleanupTestCase() {
        QFile::remove(QDir(pluginDir()).filePath(QStringLiteral("shout.lua")));
    }
};

QTEST_MAIN(PluginFlow)
#include "test_plugin_flow.moc"
