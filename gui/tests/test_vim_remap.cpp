/*
 * vim's nnoremap, as config. See docs/adr/0134.
 *
 * The whole feature is that a remapped key is indistinguishable from
 * the keys it stands for, so every case here checks the *effect* on the
 * buffer rather than that a lookup returned a string.
 */
#include "editor_viewport.h"

#include "ase/buffer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QTest>

namespace {

QString configPath() {
    return QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
        .filePath(QStringLiteral("ase/config.ase"));
}

/* The viewport reads its config on construction, so this has to land
 * before one is built. */
void writeConfig(const QString &body) {
    QDir().mkpath(QFileInfo(configPath()).path());
    QFile f(configPath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(("vim_mode = true\nanimations = false\ngit_marks = false\n" + body).toUtf8());
    f.close();
}

AseBuffer *bufferFrom(const QByteArray &text) {
    AseBuffer *buffer = ase_buffer_create();
    if (!text.isEmpty()) {
        ase_buffer_insert(buffer, 0, text.constData(), static_cast<size_t>(text.size()));
    }
    return buffer;
}

QByteArray textOf(AseBuffer *buffer) {
    size_t length = ase_buffer_length(buffer);
    QByteArray out(static_cast<int>(length), Qt::Uninitialized);
    if (length > 0) {
        ase_buffer_get_text(buffer, 0, length, out.data());
    }
    return out;
}

void type(EditorViewport &viewport, const QString &keys) {
    for (const QChar &c : keys) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(&viewport, &press);
    }
}

} // namespace

class VimRemap : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { QFile::remove(configPath()); }

    /* One key standing for another, which is what a layout remap is. */
    void aKeyCanStandForAnother() {
        writeConfig(QStringLiteral("vim.normal.n = j\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("nx"));
        QCOMPARE(textOf(buffer), QByteArray("one\nwo\nthree\n"));
    }

    /* The classic nnoremap, and the reason the value is a sequence
     * rather than a single key. */
    void aKeyCanStandForASequence() {
        writeConfig(QStringLiteral("vim.normal.Y = y$\n"));
        AseBuffer *buffer = bufferFrom("hello world\n\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 7);
        type(viewport, QStringLiteral("Y"));
        viewport.goToLineColumn(2, 1);
        type(viewport, QStringLiteral("p"));
        /* y$ from column 7 takes "world", not the whole line. */
        QCOMPARE(textOf(buffer), QByteArray("hello world\nworld\n"));
    }

    /* A count typed before a remapped key still applies to it: the
     * remap stands in for the key, not for the command. */
    void aCountCompsesWithARemappedKey() {
        writeConfig(QStringLiteral("vim.normal.n = j\n"));
        AseBuffer *buffer = bufferFrom("a\nb\nc\nd\ne\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("3nx"));
        QCOMPARE(textOf(buffer), QByteArray("a\nb\nc\n\ne\n"));
    }

    /* And an operator already pending takes a remapped motion. */
    void aPendingOperatorTakesARemappedMotion() {
        writeConfig(QStringLiteral("vim.normal.n = j\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("dn"));
        /* dj is linewise over two lines. */
        QCOMPARE(textOf(buffer), QByteArray("three\n"));
    }

    /*
     * The "nore" in nnoremap. Without it these two definitions are an
     * infinite loop rather than a pair of swapped keys.
     */
    void remapsAreNotRecursive() {
        writeConfig(QStringLiteral("vim.normal.x = dd\nvim.normal.d = x\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("x"));
        /* x ran dd, and the d inside it was not turned back into x. */
        QCOMPARE(textOf(buffer), QByteArray("two\n"));
    }

    /* The key after f is a target, not a command: remapping it would
     * make f unable to find a character you had rebound. */
    void anArgumentKeyIsNotRemapped() {
        writeConfig(QStringLiteral("vim.normal.o = j\n"));
        AseBuffer *buffer = bufferFrom("foo bar\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("fox"));
        /* f found the first o and x deleted it. */
        QCOMPARE(textOf(buffer), QByteArray("fo bar\n"));
    }

    /* Likewise the register name after a quote. */
    void aRegisterNameIsNotRemapped() {
        writeConfig(QStringLiteral("vim.normal.a = j\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\n\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("\"ayy"));
        viewport.goToLineColumn(3, 1);
        type(viewport, QStringLiteral("\"ap"));
        QCOMPARE(textOf(buffer), QByteArray("one\ntwo\n\none\n"));
    }

    /* Scoped: visual-only does not apply in Normal. */
    void aVisualRemapDoesNotApplyInNormal() {
        writeConfig(QStringLiteral("vim.visual.n = j\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("nx")); /* n is not a motion here */
        QCOMPARE(textOf(buffer), QByteArray("ne\ntwo\nthree\n"));
    }

    void anUnqualifiedRemapAppliesInBoth() {
        writeConfig(QStringLiteral("vim.n = j\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\nthree\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("vnd"));
        /* v, then a remapped j, then d. */
        QCOMPARE(textOf(buffer), QByteArray("wo\nthree\n"));
    }

    /* A mode-qualified remap beats the unqualified one, the way a
     * mode-qualified key binding already does. */
    void theModeQualifiedFormWins() {
        writeConfig(QStringLiteral("vim.n = j\nvim.normal.n = x\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("n"));
        QCOMPARE(textOf(buffer), QByteArray("ne\ntwo\n"));
    }

    /* Insert mode types letters; a Normal-mode remap must not reach it. */
    void insertModeIsUntouched() {
        writeConfig(QStringLiteral("vim.normal.n = j\n"));
        AseBuffer *buffer = bufferFrom("");
        EditorViewport viewport(buffer, QString());
        type(viewport, QStringLiteral("inn"));
        QCOMPARE(textOf(buffer), QByteArray("nn"));
    }

    /* This runs on every Normal-mode keystroke, so what it costs when
     * nothing is configured is the number that matters. */
    void benchmarkTheLookupOnTheKeystrokePath() {
        for (const char *body : {"", "vim.normal.n = j\n"}) {
            writeConfig(QString::fromLatin1(body));
            AseBuffer *buffer = bufferFrom("one two three four five\n");
            EditorViewport viewport(buffer, QString());
            viewport.resize(800, 600);

            QElapsedTimer timer;
            const int kRuns = 4000;
            timer.start();
            for (int i = 0; i < kRuns; ++i) {
                QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("l"));
                QApplication::sendEvent(&viewport, &press);
            }
            qInfo("%-14s %5lld ns per Normal-mode key", *body ? "one remap" : "no remaps",
                  static_cast<long long>(timer.nsecsElapsed() / kRuns));
            QFile::remove(configPath());
        }
    }

    /* Nothing configured must cost nothing and change nothing. */
    void anUnmappedKeyIsUnaffected() {
        writeConfig(QStringLiteral("vim.normal.n = j\n"));
        AseBuffer *buffer = bufferFrom("one\ntwo\n");
        EditorViewport viewport(buffer, QString());
        viewport.goToLineColumn(1, 1);
        type(viewport, QStringLiteral("x"));
        QCOMPARE(textOf(buffer), QByteArray("ne\ntwo\n"));
    }
};

QTEST_MAIN(VimRemap)
#include "test_vim_remap.moc"
