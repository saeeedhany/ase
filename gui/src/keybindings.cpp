#include "keybindings.h"

#include "command_registry.h"

#include "ase/config.h"
#include "ase/keymap.h"

#include <QKeyEvent>

#include <cstring>

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
        /*
         * Four layers, each with a reason — see docs/adr/0120.
         *
         *   bare keys   vim's, in the buffer. Nothing else may take one.
         *   Ctrl+x      what you bring from other editors and the OS,
         *               plus vim's own Ctrl chords.
         *   Ctrl+W x    structure: regions, their focus, size and life.
         *               vim's window prefix, with vim's window keys.
         *   Alt+x       this editor's own things, which have no
         *               cross-editor convention to inherit.
         *   F<n>        ask the language server.
         */

        /* ---- Ctrl: brought from elsewhere ---- */
        {"ctrl+s", "editor.save", ""},
        {"ctrl+shift+s", "editor.save-as", ""},
        {"ctrl+p", "editor.open-in-project", ""},
        {"ctrl+n", "buffer.new", ""},
        {"ctrl+tab", "buffer.next", ""},
        {"ctrl+shift+tab", "buffer.previous", ""},
        {"ctrl+q", "editor.quit", ""},
        {"ctrl+f", "editor.find", ""},
        {"ctrl+h", "editor.replace", ""},
        {"ctrl+c", "editor.copy", ""},
        {"ctrl+x", "editor.cut", ""},
        {"ctrl+v", "editor.paste", ""},
        {"ctrl+a", "editor.select-all", ""},
        {"ctrl+z", "editor.undo", ""},
        {"ctrl+shift+z", "editor.redo", ""},
        {"ctrl+d", "editor.cursor.add-next-occurrence", ""},
        {"ctrl+equal", "editor.font.larger", ""},
        {"ctrl+plus", "editor.font.larger", ""},
        {"ctrl+minus", "editor.font.smaller", ""},
        {"ctrl+0", "editor.font.reset", ""},

        /* vim's own Ctrl chords, which are also "brought from
         * elsewhere" — from vim. Normal and Visual only, so Insert and
         * vim_mode = false keep the editor's meanings. */
        {"ctrl+o", "editor.jump-back", ""},
        {"ctrl+i", "editor.jump-forward", ""},
        {"ctrl+d", "vim.half-page-down", "normal"},
        {"ctrl+u", "vim.half-page-up", "normal"},
        {"ctrl+d", "vim.half-page-down", "visual"},
        {"ctrl+u", "vim.half-page-up", "visual"},
        {"ctrl+a", "vim.number.increment", "normal"},
        {"ctrl+x", "vim.number.decrement", "normal"},
        {"ctrl+r", "editor.redo", "normal"},
        {"ctrl+r", "editor.redo", "insert"},
        {"ctrl+r", "editor.redo", "visual"},

        /* ---- Ctrl+W: structure, in vim's window vocabulary ---- */
        {"ctrl+w>j", "pane.focus-down", ""},
        {"ctrl+w>k", "pane.focus-up", ""},
        {"ctrl+w>w", "pane.cycle", ""},
        {"ctrl+w>c", "pane.close", ""},
        {"ctrl+w>q", "pane.close", ""},
        {"ctrl+w>o", "pane.only", ""},
        {"ctrl+w>plus", "editor.output-panel.taller", ""},
        {"ctrl+w>equal", "editor.output-panel.taller", ""},
        {"ctrl+w>minus", "editor.output-panel.shorter", ""},
        {"ctrl+w>n", "buffer.new", ""},

        /* ---- Alt: this editor's own ---- */
        {"alt+o", "editor.open", ""},
        {"alt+i", "editor.about", ""},
        {"alt+f", "editor.find-in-project", ""},
        {"alt+r", "editor.replace-in-project", ""},
        {"alt+s", "editor.document-symbols", ""},
        {"alt+b", "editor.compile", ""},
        {"alt+p", "editor.output-panel", ""},
        {"alt+semicolon", "editor.command-line", ""},
        {"alt+left", "editor.jump-back", ""},
        {"alt+right", "editor.jump-forward", ""},

        /* ---- F: ask the language server ---- */
        {"f1", "editor.help", ""},
        {"f12", "editor.go-to-definition", ""},
        {"shift+f12", "editor.find-references", ""},
        {"f2", "editor.rename-symbol", ""},
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

QString keys::vimRemap(const AseConfig *config, const QString &mode, QChar key) {
    if (config == nullptr || key.isNull()) {
        return QString();
    }
    /* On the Normal-mode keystroke path, so the same shape as
     * commandFor(): a mode-qualified lookup, then the unqualified one,
     * and nothing built unless something is there. */
    const QString qualified =
        mode.isEmpty() ? QString() : QStringLiteral("vim.%1.%2").arg(mode, key);
    const QString plain = QStringLiteral("vim.%1").arg(key);
    for (const QString &lookup : {qualified, plain}) {
        if (lookup.isEmpty()) {
            continue;
        }
        const char *mapped = ase_config_get_string(config, lookup.toUtf8().constData());
        if (mapped != nullptr && *mapped != '\0') {
            return QString::fromUtf8(mapped);
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
    /* Not an early return: a config with no `key.` settings may still
     * have `vim.` ones, and returning here meant those went unchecked
     * whenever nothing else was bound. */
    if (!ase_config_entries_with_prefix(config, "key.", &configKeys, &values, &count)) {
        count = 0;
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

    /*
     * A vim remap names a single key. `vim.normal.abc = j` would match
     * nothing and do nothing quietly, which is the failure this whole
     * function exists to prevent. See docs/adr/0134.
     */
    const char *const *vimKeys = nullptr;
    const char *const *vimValues = nullptr;
    size_t vimCount = 0;
    if (ase_config_entries_with_prefix(config, "vim.", &vimKeys, &vimValues, &vimCount)) {
        for (size_t i = 0; i < vimCount; i++) {
            QString setting = QString::fromUtf8(vimKeys[i]);
            QString rest = setting.mid(4); /* past "vim." */
            int dot = rest.indexOf(QLatin1Char('.'));
            if (dot > 0) {
                QString mode = rest.left(dot);
                if (mode != QLatin1String("normal") && mode != QLatin1String("visual")) {
                    found << QStringLiteral("%1: '%2' is not normal or visual").arg(setting, mode);
                    continue;
                }
                rest = rest.mid(dot + 1);
            }
            if (rest.size() != 1) {
                found << QStringLiteral("%1: a vim remap names one key, not '%2'")
                             .arg(setting, rest);
                continue;
            }
            if (QString::fromUtf8(vimValues[i]).isEmpty()) {
                found << QStringLiteral("%1: remapped to nothing").arg(setting);
            }
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
    /* Qt parses "Alt+;" and not "Alt+Semicolon" — it wants the symbol,
     * which is exactly what pretty() produces. Spelling the word here
     * made QKeySequence return an empty sequence, and an empty sequence
     * installs a QShortcut that can never fire: Alt+; simply did
     * nothing, with no error anywhere. See docs/adr/0122. */
    if (chord.contains(QLatin1Char(kSequenceSeparator))) {
        /* Two-chord sequences are the prefix dispatcher's, not Qt's. */
        return QKeySequence();
    }
    return QKeySequence(pretty(chord));
}

/* ---- two-chord sequences (ADR 0120) ---- */

namespace {

/* Splits "ctrl+w>j" into its two canonical halves. Returns false for a
 * plain chord, or when either half is not a chord at all. */
bool splitSequence(const QString &text, QString *first, QString *second) {
    int at = text.indexOf(QLatin1Char(keys::kSequenceSeparator));
    if (at <= 0 || at + 1 >= text.size()) {
        return false;
    }
    char one[64];
    char two[64];
    if (!ase_keymap_canonical(text.left(at).toUtf8().constData(), one, sizeof(one)) ||
        !ase_keymap_canonical(text.mid(at + 1).toUtf8().constData(), two, sizeof(two))) {
        return false;
    }
    *first = QString::fromLatin1(one);
    *second = QString::fromLatin1(two);
    return true;
}

struct Sequence {
    QString first;
    QString second;
    QString command;
};

/* The built-in table's two-chord rows, split once. These three
 * functions run on the keystroke path, and the defaults never change,
 * so canonicalising them per key press was pure waste. */
const QVector<Sequence> &defaultSequences() {
    static const QVector<Sequence> table = [] {
        QVector<Sequence> built;
        for (const keys::Binding &binding : keys::defaults()) {
            Sequence row;
            if (splitSequence(QString::fromLatin1(binding.chord), &row.first, &row.second)) {
                row.command = QString::fromLatin1(binding.command);
                built << row;
            }
        }
        return built;
    }();
    return table;
}

/* The user's two-chord bindings. The `strchr` gate is the point: an
 * ordinary `key.ctrl+s = ...` costs one byte scan, so a config without
 * sequences allocates nothing at all here. */
QVector<Sequence> configuredSequences(const AseConfig *config) {
    QVector<Sequence> found;
    const char *const *names = nullptr;
    const char *const *values = nullptr;
    size_t count = 0;
    if (config == nullptr ||
        !ase_config_entries_with_prefix(config, "key.", &names, &values, &count)) {
        return found;
    }
    for (size_t i = 0; i < count; i++) {
        if (strchr(names[i], keys::kSequenceSeparator) == nullptr) {
            continue;
        }
        Sequence row;
        if (splitSequence(QString::fromUtf8(names[i]).mid(4), &row.first, &row.second)) {
            row.command = QString::fromUtf8(values[i]);
            found << row;
        }
    }
    return found;
}

} // namespace

bool keys::isPrefix(const AseConfig *config, const QString &chord) {
    /* `none` unbinds one continuation; it does not stop the chord being
     * a prefix for the others. */
    for (const Sequence &row : configuredSequences(config)) {
        if (row.first == chord) {
            return true;
        }
    }
    for (const Sequence &row : defaultSequences()) {
        if (row.first == chord) {
            return true;
        }
    }
    return false;
}

QString keys::commandForSequence(const AseConfig *config, const QString &prefix,
                                  const QString &second) {
    for (const Sequence &row : configuredSequences(config)) {
        if (row.first == prefix && row.second == second) {
            return row.command;
        }
    }
    for (const Sequence &row : defaultSequences()) {
        if (row.first == prefix && row.second == second) {
            return row.command;
        }
    }
    return QString();
}

QStringList keys::sequenceHints(const AseConfig *config, const QString &prefix) {
    QStringList hints;
    QStringList seen;
    for (const Sequence &row : configuredSequences(config)) {
        if (row.first == prefix && !seen.contains(row.second)) {
            seen << row.second;
            if (row.command != QLatin1String("none")) {
                hints << row.second;
            }
        }
    }
    for (const Sequence &row : defaultSequences()) {
        if (row.first == prefix && !seen.contains(row.second)) {
            seen << row.second;
            hints << row.second;
        }
    }
    hints.sort();
    return hints;
}

QString keys::pretty(const QString &chord) {
    static const QHash<QString, QString> kNames = {
        {QStringLiteral("ctrl"), QStringLiteral("Ctrl")},
        {QStringLiteral("alt"), QStringLiteral("Alt")},
        {QStringLiteral("shift"), QStringLiteral("Shift")},
        {QStringLiteral("meta"), QStringLiteral("Meta")},
        {QStringLiteral("semicolon"), QStringLiteral(";")},
        {QStringLiteral("equal"), QStringLiteral("=")},
        {QStringLiteral("plus"), QStringLiteral("+")},
        {QStringLiteral("minus"), QStringLiteral("-")},
        {QStringLiteral("comma"), QStringLiteral(",")},
        {QStringLiteral("period"), QStringLiteral(".")},
        {QStringLiteral("slash"), QStringLiteral("/")},
        {QStringLiteral("apostrophe"), QStringLiteral("'")},
        {QStringLiteral("bracketleft"), QStringLiteral("[")},
        {QStringLiteral("bracketright"), QStringLiteral("]")},
        {QStringLiteral("grave"), QStringLiteral("`")},
        {QStringLiteral("backslash"), QStringLiteral("\\")},
    };

    QStringList parts;
    for (const QString &half : chord.split(QLatin1Char(kSequenceSeparator))) {
        QStringList spelled;
        for (const QString &part : half.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
            auto it = kNames.constFind(part);
            if (it != kNames.constEnd()) {
                spelled << it.value();
            } else if (part.size() == 1) {
                spelled << part.toUpper();
            } else {
                spelled << part.at(0).toUpper() + part.mid(1);
            }
        }
        parts << spelled.join(QLatin1Char('+'));
    }
    /* A space rather than the '>' the config uses: on screen these are
     * two keystrokes in order, not a path. */
    return parts.join(QLatin1Char(' '));
}
