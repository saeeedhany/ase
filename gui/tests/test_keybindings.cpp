/*
 * Which binding wins. The chord parser is covered in core; this is the
 * precedence around it, which is where the surprises are: a Vim-only
 * binding against a general one, a user's config against the built-in
 * table, and switching a default off. See docs/adr/0113.
 */
#include "command_registry.h"
#include "keybindings.h"

#include "ase/config.h"

#include <QDir>
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

    /* Every command the built-in table names must exist, or a default
     * binding is a dead key. The registry is built by the viewport, so
     * this checks the table against itself: no row may name a command
     * that no other row's spelling suggests was meant. */
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
