#ifndef ASE_LSP_REGISTRY_H
#define ASE_LSP_REGISTRY_H

#include <QObject>
#include <QString>
#include <QVector>

#include "ase/lsp_client.h"
#include "lsp_state.h"

class EditorViewport;
class QTimer;

/*
 * One language server per (language, project root), shared by every
 * buffer that wants it, instead of one per buffer.
 *
 * Owned by the window, handed to each viewport — the same shape
 * OutputPanel and CommandLine already use for "there is one of these".
 * See docs/adr/0096.
 */
class LspRegistry : public QObject {
    Q_OBJECT
public:
    explicit LspRegistry(QObject *parent = nullptr);
    ~LspRegistry() override;

    /* The shared client for this language and root, started if `user` is
     * the first to ask for it. NULL if it could not be spawned. */
    AseLspClient *acquire(EditorViewport *user, const QString &language, const QString &command,
                           const QString &rootDir);

    /* Drops `user`. The server stops when its last buffer lets go. */
    void release(EditorViewport *user);

private:
    struct Entry {
        AseLspClient *client = nullptr;
        QString language;
        QString root;
        QString serverName;
        LspState state = LspState::Starting;
        QVector<EditorViewport *> users;
    };

    void poll();
    Entry *entryForClient(const AseLspClient *client);
    void setEntryState(Entry *entry, LspState state);

    /* Heap-allocated so the diagnostics callback's user_data stays valid
     * when the vector grows. */
    QVector<Entry *> m_entries;
    QTimer *m_timer = nullptr;
};

#endif /* ASE_LSP_REGISTRY_H */
