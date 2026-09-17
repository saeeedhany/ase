#include "keybindings.h"

#include "command_registry.h"

#include "ase/config.h"
#include "ase/keymap.h"

#include <QKeyEvent>

namespace {

/* Qt's key codes to the names core/src/keymap.c knows. Letters and
 * digits are their own names and need no entry. */
struct NamedKey {
    int code;
    const char *name;
};

const NamedKey kNamedKeys[] = {
    {Qt::Key_Escape, "escape"},       {Qt::Key_Tab, "tab"},
    {Qt::Key_Return, "return"},       {Qt::Key_Enter, "return"},
    {Qt::Key_Backspace, "backspace"}, {Qt::Key_Delete, "delete"},
    {Qt::Key_Space, "space"},         {Qt::Key_Left, "left"},
    {Qt::Key_Right, "right"},         {Qt::Key_Up, "up"},
    {Qt::Key_Down, "down"},           {Qt::Key_Home, "home"},
    {Qt::Key_End, "end"},             {Qt::Key_PageUp, "pageup"},
    {Qt::Key_PageDown, "pagedown"},   {Qt::Key_Insert, "insert"},
    {Qt::Key_Semicolon, "semicolon"}, {Qt::Key_Equal, "equal"},
    {Qt::Key_Plus, "plus"},           {Qt::Key_Minus, "minus"},
    {Qt::Key_Comma, "comma"},         {Qt::Key_Period, "period"},
    {Qt::Key_Slash, "slash"},         {Qt::Key_Backslash, "backslash"},
    {Qt::Key_Apostrophe, "apostrophe"}, {Qt::Key_BracketLeft, "bracketleft"},
    {Qt::Key_BracketRight, "bracketright"}, {Qt::Key_QuoteLeft, "grave"},
};

} // namespace

const QVector<keys::Binding> &keys::defaults() {
    /* Every chord the editor answers to, in one place. Adding one here
     * is the whole of adding a shortcut; the help panel reads the same
     * table, so it cannot drift from what the keys actually do. */
    static const QVector<Binding> table = {
        /* files and buffers */
        {"ctrl+s", "editor.save", ""},
        {"ctrl+shift+s", "editor.save-as", ""},
        {"alt+o", "editor.open", ""},
        {"ctrl+p", "editor.open-in-project", ""},
        {"ctrl+n", "buffer.new", ""},
        {"ctrl+w", "buffer.close", ""},
        {"ctrl+tab", "buffer.next", ""},
        {"ctrl+shift+tab", "buffer.previous", ""},
        {"ctrl+q", "editor.quit", ""},

        /* finding */
        {"ctrl+f", "editor.find", ""},
        {"ctrl+shift+f", "editor.find-in-project", ""},
        {"ctrl+h", "editor.replace", ""},

        /* panels */
        {"f1", "editor.help", ""},
        {"alt+i", "editor.about", ""},
        {"ctrl+shift+o", "editor.output-panel", ""},
        {"ctrl+semicolon", "editor.command-line", ""},

        /* editing */
        {"ctrl+c", "editor.copy", ""},
        {"ctrl+x", "editor.cut", ""},
        {"ctrl+v", "editor.paste", ""},
        {"ctrl+a", "editor.select-all", ""},
        {"ctrl+z", "editor.undo", ""},
        {"ctrl+shift+z", "editor.redo", ""},
        {"ctrl+d", "editor.cursor.add-next-occurrence", ""},

        /* navigation */
        {"ctrl+o", "editor.jump-back", ""},
        {"alt+left", "editor.jump-back", ""},
        {"ctrl+i", "editor.jump-forward", ""},
        {"alt+right", "editor.jump-forward", ""},
        {"f12", "editor.go-to-definition", ""},
        /* Shift+F12 for references is the convention every other editor
         * uses; Ctrl+Shift+O for the outline matches Ctrl+P's "open
         * something by name" shape one level down. */
        {"shift+f12", "editor.find-references", ""},
        {"ctrl+shift+period", "editor.document-symbols", ""},

        /* view */
        {"ctrl+equal", "editor.font.larger", ""},
        {"ctrl+plus", "editor.font.larger", ""},
        {"ctrl+minus", "editor.font.smaller", ""},
        {"ctrl+0", "editor.font.reset", ""},

        /* build */
        {"ctrl+b", "editor.compile", ""},

        /* Vim takes three chords over, in Normal and Visual only —
         * Insert mode and vim_mode = false keep the editor's. See
         * docs/adr/0059 and docs/adr/0106. */
        {"ctrl+d", "vim.half-page-down", "normal"},
        {"ctrl+u", "vim.half-page-up", "normal"},
        {"ctrl+d", "vim.half-page-down", "visual"},
        {"ctrl+u", "vim.half-page-up", "visual"},
        {"ctrl+a", "vim.number.increment", "normal"},
        {"ctrl+x", "vim.number.decrement", "normal"},
        /* Vim's redo works from Insert too, so it is bound in each Vim
         * mode rather than once: with vim_mode = false the mode is
         * empty and no Ctrl+R binding matches, which is what a
         * non-Vim user should see. */
        {"ctrl+r", "editor.redo", "normal"},
        {"ctrl+r", "editor.redo", "insert"},
        {"ctrl+r", "editor.redo", "visual"},
    };
    return table;
}

QString keys::chordFor(const QKeyEvent *event) {
    if (event == nullptr) {
        return QString();
    }
    int code = event->key();

    QByteArray name;
    for (const NamedKey &named : kNamedKeys) {
        if (named.code == code) {
            name = named.name;
            break;
        }
    }
    if (name.isEmpty()) {
        if (code >= Qt::Key_F1 && code <= Qt::Key_F12) {
            name = QByteArray("f") + QByteArray::number(code - Qt::Key_F1 + 1);
        } else if ((code >= Qt::Key_A && code <= Qt::Key_Z) ||
                   (code >= Qt::Key_0 && code <= Qt::Key_9)) {
            name = QByteArray(1, static_cast<char>(QChar(code).toLower().toLatin1()));
        } else {
            return QString(); /* a key with no name cannot be bound */
        }
    }

    Qt::KeyboardModifiers mods = event->modifiers();
    char out[64];
    if (!ase_keymap_format(mods & Qt::ControlModifier, mods & Qt::AltModifier,
                            mods & Qt::ShiftModifier, mods & Qt::MetaModifier, name.constData(),
                            out, sizeof(out))) {
        return QString();
    }
    return QString::fromLatin1(out);
}

QString keys::commandFor(const AseConfig *config, const QString &chord, const QString &mode) {
    if (chord.isEmpty()) {
        return QString();
    }
    /* A mode-qualified binding beats an unqualified one, and the user's
     * config beats the defaults — in that order, so someone can rebind
     * Ctrl+D everywhere without disturbing Vim's use of it, or the other
     * way round. */
    const QString qualified = mode.isEmpty() ? QString()
                                             : QStringLiteral("key.%1.%2").arg(mode, chord);
    const QString plain = QStringLiteral("key.%1").arg(chord);

    if (config != nullptr) {
        for (const QString &lookup : {qualified, plain}) {
            if (lookup.isEmpty()) {
                continue;
            }
            const char *bound = ase_config_get_string(config, lookup.toUtf8().constData());
            if (bound != nullptr && *bound != '\0') {
                return QString::fromUtf8(bound);
            }
        }
    }

    for (const Binding &binding : defaults()) {
        if (!mode.isEmpty() && mode == QLatin1String(binding.mode) &&
            chord == QLatin1String(binding.chord)) {
            return QString::fromLatin1(binding.command);
        }
    }
    for (const Binding &binding : defaults()) {
        if (*binding.mode == '\0' && chord == QLatin1String(binding.chord)) {
            return QString::fromLatin1(binding.command);
        }
    }
    return QString();
}

QStringList keys::problems(const AseConfig *config, const CommandRegistry &registry,
                            const QStringList &alsoKnown) {
    QStringList found;
    if (config == nullptr) {
        return found;
    }
    const char *const *configKeys = nullptr;
    const char *const *values = nullptr;
    size_t count = 0;
    if (!ase_config_entries_with_prefix(config, "key.", &configKeys, &values, &count)) {
        return found;
    }

    for (size_t i = 0; i < count; i++) {
        QString setting = QString::fromUtf8(configKeys[i]);
        QString command = QString::fromUtf8(values[i]);
        QString chord = setting.mid(4); /* past "key." */

        /* A mode prefix, if there is one: key.normal.ctrl+d. */
        int dot = chord.indexOf(QLatin1Char('.'));
        if (dot > 0) {
            QString mode = chord.left(dot);
            if (mode == QLatin1String("normal") || mode == QLatin1String("insert") ||
                mode == QLatin1String("visual")) {
                chord = chord.mid(dot + 1);
            }
        }

        char canonical[64];
        if (!ase_keymap_canonical(chord.toUtf8().constData(), canonical, sizeof(canonical))) {
            found << QStringLiteral("%1: '%2' is not a key").arg(setting, chord);
            continue;
        }
        /* "none" is how a default is switched off, not a command. */
        if (command == QLatin1String("none")) {
            continue;
        }
        if (!registry.contains(command) && !alsoKnown.contains(command)) {
            found << QStringLiteral("%1: no command called '%2'").arg(setting, command);
        }
    }
    return found;
}

QStringList keys::chordsFor(const AseConfig *config, const QString &command) {
    QStringList chords;
    QStringList rebound; /* chords the user has pointed at something else */

    const char *const *configKeys = nullptr;
    const char *const *values = nullptr;
    size_t count = 0;
    if (config != nullptr &&
        ase_config_entries_with_prefix(config, "key.", &configKeys, &values, &count)) {
        for (size_t i = 0; i < count; i++) {
            QString chord = QString::fromUtf8(configKeys[i]).mid(4);
            /* Window shortcuts have no mode, so a mode-qualified binding
             * is not one of theirs. */
            if (chord.contains(QLatin1Char('.'))) {
                continue;
            }
            char canonical[64];
            if (!ase_keymap_canonical(chord.toUtf8().constData(), canonical, sizeof(canonical))) {
                continue;
            }
            QString spelled = QString::fromLatin1(canonical);
            if (QString::fromUtf8(values[i]) == command) {
                chords << spelled;
            } else {
                rebound << spelled;
            }
        }
    }

    for (const Binding &binding : defaults()) {
        if (*binding.mode != '\0' || command != QLatin1String(binding.command)) {
            continue;
        }
        QString chord = QString::fromLatin1(binding.chord);
        if (!rebound.contains(chord) && !chords.contains(chord)) {
            chords << chord;
        }
    }
    return chords;
}

QKeySequence keys::sequenceFor(const QString &chord) {
    /* Qt parses "Ctrl+Shift+F"; the canonical form differs only in case
     * and in naming punctuation keys by word, which Qt also accepts. */
    QStringList parts = chord.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (chord.endsWith(QLatin1String("+plus"))) {
        parts = chord.left(chord.size() - 5).split(QLatin1Char('+'), Qt::SkipEmptyParts);
        parts << QStringLiteral("+");
    }
    QStringList spelled;
    for (const QString &part : parts) {
        spelled << (part.size() == 1 ? part.toUpper()
                                     : part.at(0).toUpper() + part.mid(1));
    }
    QKeySequence sequence(spelled.join(QLatin1Char('+')));
    return sequence;
}
