#ifndef ASE_KEYBINDINGS_H
#define ASE_KEYBINDINGS_H

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVector>

class QKeyEvent;
class CommandRegistry;
struct AseConfig;

/*
 * Which chord runs which command.
 *
 * The defaults are a table rather than a chain of `if (key == ...)`, so
 * the same lookup serves a user's `key.ctrl+s = editor.compile` and the
 * built-in binding it replaces. See docs/adr/0113.
 */
namespace keys {

struct Binding {
    const char *chord;   /* canonical form, as core/src/keymap.c spells it */
    const char *command;
    const char *mode;    /* "", or "normal"/"insert"/"visual" for a Vim-only binding */
};

/* The built-in table. */
const QVector<Binding> &defaults();

/* The chord a key press names, canonically, or empty when the event
 * carries no key this editor can name. */
QString chordFor(const QKeyEvent *event);

/* The command bound to `chord`, preferring a binding for `mode` over an
 * unqualified one, and the user's config over the defaults. Empty when
 * nothing is bound; the literal "none" when a binding disables a
 * default. */
QString commandFor(const AseConfig *config, const QString &chord, const QString &mode);

/* Every problem in the user's `key.*` settings: an unparseable chord, or
 * a command no one registered. Returned rather than printed so the
 * caller decides how to say it. */
QStringList problems(const AseConfig *config, const CommandRegistry &registry,
                     const QStringList &alsoKnown = QStringList());

/* Every chord that runs `command`: the user's bindings for it, plus any
 * default the user has not pointed somewhere else. The reverse of
 * commandFor, for the window shortcuts, which have to be built from a
 * chord rather than looked up when one arrives. */
QStringList chordsFor(const AseConfig *config, const QString &command);

/* A canonical chord as Qt spells it, for a QShortcut. Empty when Qt has
 * no sequence for it. */
QKeySequence sequenceFor(const QString &chord);

} // namespace keys

#endif
