/*
 * A plugin command through a real EditorViewport. Two things are being
 * asked: what the undo stack holds afterwards, since the plugin edits
 * the buffer underneath it (docs/adr/0128), and what the plugin can
 * reach besides the text (docs/adr/0141).
 */
#include "editor_viewport.h"

#include "command_registry.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

QString pluginDir() {
    return QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
        .filePath(QStringLiteral("ase/plugins"));
}

void writePlugin(const QString &name, const QByteArray &source) {
    QFile plugin(QDir(pluginDir()).filePath(name));
    QVERIFY(plugin.open(QIODevice::WriteOnly | QIODevice::Text));
    plugin.write(source);
    plugin.close();
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

/* The hook plugin keeps its counts in Lua; :report posts them to the
 * status bar, which is the only way back out. */
QHash<QString, int> counts(const QString &status) {
    QHash<QString, int> out;
    for (const QString &pair : status.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QStringList halves = pair.split(QLatin1Char('='));
        if (halves.size() == 2) {
            out.insert(halves.at(0), halves.at(1).toInt());
        }
    }
    return out;
}

} // namespace

class PluginFlow : public QObject {
    Q_OBJECT

private:
    CommandRegistry m_windowCommands;

    QHash<QString, int> report(EditorViewport &viewport) {
        QSignalSpy said(&viewport, &EditorViewport::messagePosted);
        viewport.runCommandByName(QStringLiteral("report"));
        if (said.isEmpty()) {
            return {};
        }
        return counts(said.last().at(1).toString());
    }

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

        /* What ABI 1 made impossible: reaching the caret, the selection,
         * the config and the status line. */
        writePlugin(QStringLiteral("context.lua"),
                    "ase.register_command(\"mark_here\", function(ctx)\n"
                    "    ase.buffer_insert(ctx, ase.cursor(ctx), \"<>\")\n"
                    "end)\n"
                    "ase.register_command(\"bracket\", function(ctx)\n"
                    "    local a, b = ase.selection(ctx)\n"
                    "    if a == nil then\n"
                    "        ase.status(ctx, \"nothing selected\")\n"
                    "        return\n"
                    "    end\n"
                    "    ase.buffer_insert(ctx, b, \"]\")\n"
                    "    ase.buffer_insert(ctx, a, \"[\")\n"
                    "    ase.set_cursor(ctx, a)\n"
                    "end)\n"
                    "ase.register_command(\"go_home\", function(ctx)\n"
                    "    ase.set_cursor(ctx, 0)\n"
                    "end)\n"
                    "ase.register_command(\"say_font\", function(ctx)\n"
                    "    ase.status(ctx, \"font=\" .. tostring(ase.config(ctx, \"font_family\")))\n"
                    "end)\n"
                    "ase.register_command(\"far_off\", function(ctx)\n"
                    "    ase.set_cursor(ctx, 9999)\n"
                    "end)\n");

        /* Hooks (ADR 0142). Each one counts itself into a buffer the
         * test can read, since a hook returns nothing. */
        writePlugin(QStringLiteral("hooks.lua"),
                    "counts = {changed = 0, moved = 0, saved = 0, opened = 0}\n"
                    "ase.on(\"buffer_changed\", function(ctx) counts.changed = counts.changed + 1 end)\n"
                    "ase.on(\"cursor_moved\", function(ctx) counts.moved = counts.moved + 1 end)\n"
                    "ase.on(\"file_saved\", function(ctx) counts.saved = counts.saved + 1 end)\n"
                    "ase.on(\"file_opened\", function(ctx) counts.opened = counts.opened + 1 end)\n"
                    "ase.register_command(\"report\", function(ctx)\n"
                    "    ase.status(ctx, string.format(\"c=%d m=%d s=%d o=%d\",\n"
                    "        counts.changed, counts.moved, counts.saved, counts.opened))\n"
                    "end)\n"
                    "ase.register_command(\"reset_counts\", function(ctx)\n"
                    "    counts = {changed = 0, moved = 0, saved = 0, opened = 0}\n"
                    "end)\n"
                    /* A hook that edits: proof that a hook's own edit
                     * does not raise the event that called it. */
                    "ase.on(\"file_saved\", function(ctx)\n"
                    "    ase.buffer_insert(ctx, 0, \"!\")\n"
                    "end)\n");
    }

    /* ---- events (ADR 0142) ---- */

    /* Typing a burst is one buffer_changed, not one per keystroke —
     * the whole reason these are coalesced. */
    void aBurstOfTypingIsOneEvent() {
        AseBuffer *buffer = bufferFrom("");
        EditorViewport viewport(buffer, QString());
        QVERIFY(viewport.runCommandByName(QStringLiteral("reset_counts")));

        type(viewport, QStringLiteral("ihello world"));
        press(viewport, Qt::Key_Escape);
        QCOMPARE(report(viewport).value(QStringLiteral("c")), 0); /* nothing yet */

        QTest::qWait(150);
        QCOMPARE(report(viewport).value(QStringLiteral("c")), 1);
        QCOMPARE(report(viewport).value(QStringLiteral("m")), 1);
    }

    /* Moving without typing is a cursor_moved and no buffer_changed. */
    void movingTheCaretIsNotAnEdit() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        QTest::qWait(150);
        QVERIFY(viewport.runCommandByName(QStringLiteral("reset_counts")));

        press(viewport, Qt::Key_J, QStringLiteral("j"));
        press(viewport, Qt::Key_J, QStringLiteral("j"));
        QTest::qWait(150);

        QCOMPARE(report(viewport).value(QStringLiteral("m")), 1);
        QCOMPARE(report(viewport).value(QStringLiteral("c")), 0);
    }

    /* A caret that ends where it started did not move. */
    void aRoundTripIsNotAMove() {
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        QTest::qWait(150);
        QVERIFY(viewport.runCommandByName(QStringLiteral("reset_counts")));

        press(viewport, Qt::Key_J, QStringLiteral("j"));
        press(viewport, Qt::Key_K, QStringLiteral("k"));
        QTest::qWait(150);

        QCOMPARE(report(viewport).value(QStringLiteral("m")), 0);
    }

    void openingAFileIsAnEvent() {
        AseBuffer *buffer = bufferFrom("x\n");
        EditorViewport viewport(buffer, QString());
        QTest::qWait(150);
        QCOMPARE(report(viewport).value(QStringLiteral("o")), 1);
    }

    /* Saving fires after the write, and the hook that edits on save
     * does not set off a cascade. */
    void savingIsAnEventAndAHookMayEdit() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("saved.txt"));

        AseBuffer *buffer = bufferFrom("body\n");
        EditorViewport viewport(buffer, path);
        viewport.registerCommands(&m_windowCommands); /* :editor.save is a built-in */
        QTest::qWait(150);
        QVERIFY(viewport.runCommandByName(QStringLiteral("reset_counts")));

        QVERIFY(viewport.runCommandByName(QStringLiteral("editor.save")));
        QCOMPARE(report(viewport).value(QStringLiteral("s")), 1);
        /* The hook's insert landed. */
        QCOMPARE(textOf(buffer), QByteArray("!body\n"));

        /* And settled: the hook's own edit did not raise buffer_changed,
         * and nothing kept firing. */
        QTest::qWait(200);
        QCOMPARE(report(viewport).value(QStringLiteral("s")), 1);
        QCOMPARE(report(viewport).value(QStringLiteral("c")), 0);
    }

    /* ---- what the context reaches (ADR 0141) ---- */

    void aPluginSeesWhereTheCaretIs() {
        AseBuffer *buffer = bufferFrom("hello world\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(5, 0);

        QVERIFY(viewport.runCommandByName(QStringLiteral("mark_here")));
        QCOMPARE(textOf(buffer), QByteArray("hello<> world\n"));
    }

    void aPluginMovesTheCaret() {
        AseBuffer *buffer = bufferFrom("hello world\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(7, 0);

        QVERIFY(viewport.runCommandByName(QStringLiteral("go_home")));
        QCOMPARE(viewport.cursorOffset(), size_t(0));
    }

    /* A caret past the end of what the plugin left behind is the crash
     * this clamp exists for. */
    void aCaretPastTheEndIsClamped() {
        AseBuffer *buffer = bufferFrom("hello\n");
        EditorViewport viewport(buffer, QString());
        QVERIFY(viewport.runCommandByName(QStringLiteral("far_off")));
        QCOMPARE(viewport.cursorOffset(), size_t(6));
    }

    void aPluginReadsConfigAndSaysSomething() {
        EditorViewport viewport(bufferFrom("x\n"), QString());
        QSignalSpy said(&viewport, &EditorViewport::messagePosted);

        QVERIFY(viewport.runCommandByName(QStringLiteral("say_font")));
        QCOMPARE(said.count(), 1);
        QVERIFY(said.first().at(1).toString().startsWith(QStringLiteral("font=")));
    }

    /* Surround-the-selection is the plugin everyone writes first, and
     * ABI 1 could not express it at all. */
    void aPluginSurroundsTheSelection() {
        AseBuffer *buffer = bufferFrom("hello world\n");
        EditorViewport viewport(buffer, QString());
        viewport.restorePosition(0, 0);
        press(viewport, Qt::Key_V, QStringLiteral("v"));
        for (int i = 0; i < 4; ++i) {
            press(viewport, Qt::Key_L, QStringLiteral("l"));
        }

        QVERIFY(viewport.runCommandByName(QStringLiteral("bracket")));
        QCOMPARE(textOf(buffer), QByteArray("[hello] world\n"));
    }

    void withNothingSelectedTheSameCommandSaysSo() {
        EditorViewport viewport(bufferFrom("hello world\n"), QString());
        QSignalSpy said(&viewport, &EditorViewport::messagePosted);

        QVERIFY(viewport.runCommandByName(QStringLiteral("bracket")));
        QCOMPARE(said.count(), 1);
        QCOMPARE(said.first().at(1).toString(), QStringLiteral("nothing selected"));
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
        for (const QString &name : {QStringLiteral("shout.lua"), QStringLiteral("context.lua"),
                                     QStringLiteral("hooks.lua")}) {
            QFile::remove(QDir(pluginDir()).filePath(name));
        }
    }
};

QTEST_MAIN(PluginFlow)
#include "test_plugin_flow.moc"
