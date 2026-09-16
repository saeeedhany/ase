#ifndef ASE_COMMAND_REGISTRY_H
#define ASE_COMMAND_REGISTRY_H

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

/*
 * Every action the editor can be asked to perform, under a name.
 *
 * The point is that there is one of these rather than one list for
 * built-ins and another for plugins: rebinding Ctrl+S and binding a key
 * to a plugin command are then the same mechanism, and neither needs new
 * API. See docs/adr/0113.
 *
 * Names are dotted and stable, because a config file refers to them:
 * `editor.save`, `buffer.close`, `vim.half-page-down`.
 */
class CommandRegistry {
public:
    struct Command {
        QString name;
        QString description; /* shown in the help panel */
        std::function<void()> run;
    };

    void add(const QString &name, const QString &description, std::function<void()> run) {
        m_commands.insert(name, Command{name, description, std::move(run)});
    }

    bool contains(const QString &name) const { return m_commands.contains(name); }

    /* False when no command has that name — the caller reports it rather
     * than doing nothing, since a misspelled binding that silently does
     * nothing is indistinguishable from a broken key. */
    bool run(const QString &name) const {
        auto it = m_commands.constFind(name);
        if (it == m_commands.constEnd() || !it->run) {
            return false;
        }
        it->run();
        return true;
    }

    QString describe(const QString &name) const {
        auto it = m_commands.constFind(name);
        return it == m_commands.constEnd() ? QString() : it->description;
    }

    QStringList names() const {
        QStringList out = m_commands.keys();
        out.sort();
        return out;
    }

private:
    QHash<QString, Command> m_commands;
};

#endif
