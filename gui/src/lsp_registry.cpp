#include "lsp_registry.h"

#include "editor_viewport.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

namespace {
/* Borrowed for the duration of the call, so every user is dispatched to
 * synchronously — each one filters by URI itself. */
void diagnosticsTrampoline(void *user_data, const char *uri, const AseLspDiagnostic *diagnostics,
                            size_t count) {
    auto *users = static_cast<QVector<EditorViewport *> *>(user_data);
    for (EditorViewport *viewport : *users) {
        viewport->applyLspDiagnostics(uri, diagnostics, count);
    }
}
} // namespace

LspRegistry::LspRegistry(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &LspRegistry::poll);
    /* One timer for every server, where each buffer used to bring its
     * own — the same beat as before (docs/adr/0029). */
    m_timer->start(200);
}

LspRegistry::~LspRegistry() {
    for (Entry *entry : m_entries) {
        ase_lsp_client_stop(entry->client);
        delete entry;
    }
}

AseLspClient *LspRegistry::acquire(EditorViewport *user, const QString &language,
                                    const QString &command, const QString &rootDir) {
    for (Entry *entry : m_entries) {
        if (entry->language == language && entry->root == rootDir) {
            if (!entry->users.contains(user)) {
                entry->users.push_back(user);
            }
            /* Tell the newcomer where the server already got to, rather
             * than leaving it showing whatever it had. */
            user->onLspStateChanged(entry->state, entry->serverName);
            return entry->client;
        }
    }

    auto *entry = new Entry();
    entry->language = language;
    entry->root = rootDir;
    entry->serverName = QFileInfo(command).fileName();
    entry->users.push_back(user);

    /* clangd needs a root to find compile_commands.json and to index
     * across files; it was never given one before. */
    QByteArray commandBytes = command.toLocal8Bit();
    QByteArray rootUri = QUrl::fromLocalFile(rootDir).toString().toUtf8();
    const char *argv[] = {commandBytes.constData(), nullptr};
    entry->client = ase_lsp_client_start(argv, rootUri.constData());

    if (entry->client == nullptr) {
        entry->state = LspState::Failed;
        user->onLspStateChanged(entry->state, entry->serverName);
        delete entry;
        return nullptr;
    }

    entry->state = LspState::Starting;
    /* The users vector's address, not the entry's: entries are heap
     * allocated so this stays valid, and the callback wants the list. */
    ase_lsp_client_set_diagnostics_callback(entry->client, diagnosticsTrampoline, &entry->users);
    m_entries.push_back(entry);
    user->onLspStateChanged(entry->state, entry->serverName);
    return entry->client;
}

void LspRegistry::release(EditorViewport *user) {
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry *entry = m_entries[i];
        if (!entry->users.contains(user)) {
            continue;
        }
        entry->users.removeAll(user);
        if (!entry->users.isEmpty()) {
            return;
        }
        /* Last buffer for this server: nothing left to diagnose. */
        ase_lsp_client_stop(entry->client);
        delete entry;
        m_entries.remove(i);
        return;
    }
}

LspRegistry::Entry *LspRegistry::entryForClient(const AseLspClient *client) {
    for (Entry *entry : m_entries) {
        if (entry->client == client) {
            return entry;
        }
    }
    return nullptr;
}

void LspRegistry::setEntryState(Entry *entry, LspState state) {
    if (entry->state == state) {
        return;
    }
    entry->state = state;
    for (EditorViewport *viewport : entry->users) {
        viewport->onLspStateChanged(state, entry->serverName);
    }
}

void LspRegistry::poll() {
    for (Entry *entry : m_entries) {
        ase_lsp_client_poll(entry->client);

        if (entry->state == LspState::Starting) {
            if (ase_lsp_client_is_ready(entry->client)) {
                setEntryState(entry, LspState::Running);
            } else if (!ase_lsp_client_is_alive(entry->client)) {
                setEntryState(entry, LspState::Failed);
            }
            continue;
        }
        if (entry->state == LspState::Running && !ase_lsp_client_is_alive(entry->client)) {
            setEntryState(entry, LspState::Stopped);
        }
    }
}
