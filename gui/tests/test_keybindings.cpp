/*
 * Which binding wins. The chord parser is covered in core; this is the
 * precedence around it, which is where the surprises are: a Vim-only
 * binding against a general one, a user's config against the built-in
 * table, and switching a default off. See docs/adr/0113.
 */
#include "command_registry.h"
#include <QFile>
#include <QApplication>
#include <QKeyEvent>
#include "ase/buffer.h"
#include "editor_viewport.h"
#include "keybindings.h"

#include "ase/config.h"

#include <QDir>
#include <QElapsedTimer>
#include <QTest>

namespace {

/* A config written to disk and loaded, rather than a fake: the point is
 * that what a user types into config.ase reaches the lookup. */
AseConfig *configFrom(const QByteArray &text) {
    QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("ase_keybindings_test.conf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return nullptr;
    }
    file.write(text);
    file.close();
    AseConfig *config = ase_config_load(path.toUtf8().constData());
    QFile::remove(path);
    return config;
}

} // namespace

class Keybindings : public QObject {
    Q_OBJECT

private slots:
    /* With no config at all, the built-in table is what answers. */
    void defaultsApplyWithNoConfig() {
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("ctrl+s"), QString()),
                 QStringLiteral("editor.save"));
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("f1"), QString()),
                 QStringLiteral("editor.help"));
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("ctrl+j"), QString()), QString());
    }

    /* Vim takes Ctrl+D over in Normal mode and leaves it alone
     * elsewhere — the behaviour ADR 0059 argued for, now expressed as
     * two rows rather than an if. */
    void modeQualifiedBindingWins() {
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("ctrl+d"), QStringLiteral("normal")),
                 QStringLiteral("vim.half-page-down"));
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("ctrl+d"), QStringLiteral("visual")),
                 QStringLiteral("vim.half-page-down"));
        /* Insert mode has no Vim binding for it, so the general one. */
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("ctrl+d"), QStringLiteral("insert")),
                 QStringLiteral("editor.cursor.add-next-occurrence"));
        /* Vim off: the mode is empty and the general one applies. */
        QCOMPARE(keys::commandFor(nullptr, QStringLiteral("ctrl+d"), QString()),
                 QStringLiteral("editor.cursor.add-next-occurrence"));
    }

    void configOverridesTheDefault() {
        AseConfig *config = configFrom("key.ctrl+s = editor.compile\n");
        QCOMPARE(keys::commandFor(config, QStringLiteral("ctrl+s"), QString()),
                 QStringLiteral("editor.compile"));
        ase_config_destroy(config);
    }

    /* A chord with no default at all becomes usable. */
    void configCanBindAnUnusedChord() {
        AseConfig *config = configFrom("key.f5 = editor.save\n");
        QCOMPARE(keys::commandFor(config, QStringLiteral("f5"), QString()),
                 QStringLiteral("editor.save"));
        ase_config_destroy(config);
    }

    /* The user's spelling need not match the canonical one. */
    void configAcceptsAnySpelling() {
        AseConfig *config = configFrom("key.Ctrl+Shift+F = editor.save\n");
        /* The lookup builds the canonical form from the key press, so a
         * config written the other way round has to still match. */
        QCOMPARE(keys::chordsFor(config, QStringLiteral("editor.save"))
                     .contains(QStringLiteral("ctrl+shift+f")),
                 true);
        ase_config_destroy(config);
    }

    void noneSwitchesADefaultOff() {
        AseConfig *config = configFrom("key.ctrl+s = none\n");
        QCOMPARE(keys::commandFor(config, QStringLiteral("ctrl+s"), QString()),
                 QStringLiteral("none"));
        ase_config_destroy(config);
    }

    /* A mode-qualified override must not disturb the general binding. */
    void configCanBindOneModeOnly() {
        AseConfig *config = configFrom("key.normal.ctrl+d = editor.save\n");
        QCOMPARE(keys::commandFor(config, QStringLiteral("ctrl+d"), QStringLiteral("normal")),
                 QStringLiteral("editor.save"));
        QCOMPARE(keys::commandFor(config, QStringLiteral("ctrl+d"), QStringLiteral("insert")),
                 QStringLiteral("editor.cursor.add-next-occurrence"));
        ase_config_destroy(config);
    }

    /* The reverse lookup, which the window shortcuts and the help panel
     * both depend on. */
    void chordsForReportsBothSources() {
        AseConfig *config = configFrom("key.f2 = editor.open\n");
        QStringList chords = keys::chordsFor(config, QStringLiteral("editor.open"));
        QVERIFY(chords.contains(QStringLiteral("f2")));      /* the user's */
        QVERIFY(chords.contains(QStringLiteral("alt+o")));   /* still the default */
        ase_config_destroy(config);
    }

    /* A default pointed elsewhere must stop being reported for its old
     * command, or the help panel would list a key that does something
     * else now. */
    void chordsForDropsAReboundDefault() {
        AseConfig *config = configFrom("key.alt+o = editor.compile\n");
        QStringList chords = keys::chordsFor(config, QStringLiteral("editor.open"));
        QVERIFY(!chords.contains(QStringLiteral("alt+o")));
        ase_config_destroy(config);
    }

    void problemsReportsNonsense() {
        CommandRegistry registry;
        registry.add(QStringLiteral("editor.save"), QStringLiteral("Save"), []() {});

        AseConfig *config = configFrom("key.ctrl+s = editor.save\n"
                                        "key.ctrl+g = editor.nope\n"
                                        "key.nosuchkey = editor.save\n"
                                        "key.ctrl+q = none\n");
        QStringList found = keys::problems(config, registry);
        /* The good binding and the `none` are fine; the other two are not. */
        QCOMPARE(found.size(), 2);
        QVERIFY(found.join(QLatin1Char('\n')).contains(QStringLiteral("editor.nope")));
        QVERIFY(found.join(QLatin1Char('\n')).contains(QStringLiteral("nosuchkey")));
        ase_config_destroy(config);
    }

    /* Both run for every key press, so their cost is the floor under
     * typing. isPrefix() once rebuilt two QStringLists and canonicalised
     * every `key.*` setting per keystroke: 4584ns, against 212 now. */
    void lookupStaysOffTheAllocator() {
        AseConfig *config = configFrom("key.ctrl+s = editor.save\n"
                                        "key.alt+q = editor.quit\n");
        QElapsedTimer timer;
        const int kRuns = 20000;

        /* What the application filter does for every key press. */
        timer.start();
        for (int i = 0; i < kRuns; ++i) {
            (void)keys::isPrefix(config, QStringLiteral("ctrl+j"));
        }
        qint64 prefixNs = timer.nsecsElapsed() / kRuns;

        timer.start();
        for (int i = 0; i < kRuns; ++i) {
            (void)keys::commandFor(config, QStringLiteral("ctrl+j"), QStringLiteral("normal"));
        }
        qint64 commandNs = timer.nsecsElapsed() / kRuns;

        qInfo("isPrefix   %lld ns/key", static_cast<long long>(prefixNs));
        qInfo("commandFor %lld ns/key", static_cast<long long>(commandNs));
        qInfo("total      %lld ns/key", static_cast<long long>(prefixNs + commandNs));
        ase_config_destroy(config);

        /* A ratio, not a number of nanoseconds: both halves take the
         * same penalty from a sanitizer or a loaded machine, so this
         * holds where an absolute bound does not. commandFor() does two
         * config lookups and builds two QStrings; isPrefix() should cost
         * less than that. Allocating per `key.*` entry made it 8.5x
         * more. */
        QVERIFY2(prefixNs < commandNs * 2,
                  qPrintable(QStringLiteral("isPrefix %1ns against commandFor %2ns")
                                 .arg(prefixNs)
                                 .arg(commandNs)));
    }

    /* A key event into a real viewport with a real file, which is what a
     * keystroke actually costs.
     *
     * Past kSyncHighlightBytes the parse is meant to wait for a pause in
     * typing. It did not: refreshCache() zeroed the capture window every
     * keystroke, so ensureCursorVisible() — which needs captures to
     * measure the caret — re-parsed the whole file anyway. The debounce
     * bought nothing, and a 290KB file cost 4008us a key against 56 for
     * the same size without a grammar. See docs/adr/0124. */
    void typingCostByFileSize() {
        /* .c gets a grammar, .txt does not — the difference between the
         * two runs is what the syntax layer costs. */
        QVector<qint64> withGrammar;
        QVector<qint64> without;
        for (int lines : {500, 2000, 5000, 10000, 20000}) {
        for (const char *suffix : {".c", ".txt"}) {
        QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("ase_bench") +
                                                        QLatin1String(suffix));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray body;
        for (int i = 0; i < lines; ++i) {
            body += QByteArray("static int value_") + QByteArray::number(i) + " = " +
                    QByteArray::number(i) + ";\n";
        }
        file.write(body);
        file.close();

        AseBuffer *buffer = ase_buffer_create();
        ase_buffer_insert(buffer, 0, body.constData(), static_cast<size_t>(body.size()));
        EditorViewport viewport(buffer, path);
        viewport.resize(900, 650);

        QElapsedTimer timer;
        const int kRuns = 60;
        /* Insert mode, so each key is a real edit: refreshCache, the
         * line scan, the syntax window, the lot. */
        QKeyEvent enterInsert(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, QStringLiteral("i"));
        QApplication::sendEvent(&viewport, &enterInsert);

        timer.start();
        for (int i = 0; i < kRuns; ++i) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
            QApplication::sendEvent(&viewport, &press);
        }
        qint64 perKey = timer.nsecsElapsed() / kRuns / 1000;
        qInfo("%6d lines (%4lld KB) %-5s: %5lld us per keystroke", lines,
              static_cast<long long>(body.size() / 1024), suffix,
              static_cast<long long>(perKey));
        if (body.size() > 256 * 1024) {
            (QLatin1String(suffix) == QLatin1String(".c") ? withGrammar : without) << perKey;
        }

        QFile::remove(path);
        }
        }

        /* Above the threshold a grammar should cost almost nothing,
         * because no parse runs until typing stops. Generous against a
         * loaded machine; the defeated debounce was 71x. */
        QCOMPARE(withGrammar.size(), without.size());
        for (int i = 0; i < withGrammar.size(); ++i) {
            QVERIFY2(withGrammar[i] < without[i] * 5 + 200,
                      qPrintable(QStringLiteral("deferred highlight is not deferred: "
                                                "%1us with a grammar, %2us without")
                                     .arg(withGrammar[i])
                                     .arg(without[i])));
        }
    }

    void defaultTableHasNoEmptyFields() {
        for (const keys::Binding &binding : keys::defaults()) {
            QVERIFY(binding.chord != nullptr && *binding.chord != '\0');
            QVERIFY(binding.command != nullptr && *binding.command != '\0');
            QVERIFY(binding.mode != nullptr);
        }
    }
};

QTEST_MAIN(Keybindings)
#include "test_keybindings.moc"
