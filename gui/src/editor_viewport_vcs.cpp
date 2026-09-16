#include "editor_viewport.h"

#include "ase/config.h"
#include "ase/process.h"
#include "ase/vcs.h"

#include <cstring>

#include <QDir>
#include <QFileInfo>
#include <QTimer>

/*
 * Gutter marks for what changed since the last commit. git is spawned
 * and polled the way :compile already is (ADR 0029): never waited on,
 * because a cold repository or a network filesystem can make it slow
 * and a blocked paint is worse than a late mark. See docs/adr/0112.
 */

/* A `.git` entry at or above `startDir`. It is a file rather than a
 * directory inside a worktree or a submodule, so existence is the test,
 * not type. */
static bool isInsideGitRepository(const QString &startDir) {
    QDir dir(startDir);
    while (true) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git")))) {
            return true;
        }
        if (dir.isRoot() || !dir.cdUp()) {
            return false;
        }
    }
}

void EditorViewport::refreshVcsMarks() {
    if (m_filePath.isEmpty() || m_vcsProcess != nullptr) {
        return;
    }
    const char *enabled = ase_config_get_string(m_config, "git_marks");
    if (enabled != nullptr && strcmp(enabled, "false") == 0) {
        return;
    }
    QFileInfo info(m_filePath);
    /* Outside a repository `git diff -- <path>` does not fail cleanly:
     * it falls back to --no-index, warns, and prints seven kilobytes of
     * usage with exit 129. Looking for the .git entry first means git is
     * never spawned where it has nothing to say.
     *
     * Not project::rootFor(): that answers "where does this project
     * start", and when it finds no .git it returns the directory it was
     * given rather than nothing. Using it as a yes/no test silently
     * passed every file. */
    if (!isInsideGitRepository(info.absolutePath())) {
        return;
    }
    QByteArray dir = info.absolutePath().toUtf8();
    QByteArray name = info.absoluteFilePath().toUtf8();

    /* -U0 so every hunk header is exact rather than padded with three
     * lines of context. --no-ext-diff and --no-color in case the user's
     * gitconfig has opinions; -- so a file named like an option is still
     * a file. */
    const char *argv[] = {"git",        "--no-pager", "diff",        "--no-ext-diff",
                          "--no-color", "-U0",        "--",          name.constData(),
                          nullptr};
    m_vcsProcess = ase_process_spawn(argv, dir.constData());
    if (m_vcsProcess == nullptr) {
        return; /* no git on PATH: no marks, and nothing to report */
    }
    m_vcsOutput.clear();
    if (m_vcsPollTimer == nullptr) {
        m_vcsPollTimer = new QTimer(this);
        connect(m_vcsPollTimer, &QTimer::timeout, this, [this]() { pollVcs(); });
    }
    m_vcsPollTimer->start(60);
}

void EditorViewport::pollVcs() {
    if (m_vcsProcess == nullptr) {
        m_vcsPollTimer->stop();
        return;
    }

    char buf[8192];
    for (;;) {
        long n = ase_process_read(m_vcsProcess, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        /* Only the hunk headers are ever read, but git has no way to
         * emit those alone, so the body arrives too. A pathological
         * diff is not worth unbounded memory. */
        if (m_vcsOutput.size() < kVcsOutputCap) {
            m_vcsOutput.append(buf, static_cast<int>(n));
        }
    }

    if (!ase_process_has_exited(m_vcsProcess)) {
        return;
    }
    int exitCode = ase_process_exit_code(m_vcsProcess);
    ase_process_destroy(m_vcsProcess);
    m_vcsProcess = nullptr;
    m_vcsPollTimer->stop();

    ase_vcs_diff_destroy(m_vcsDiff);
    m_vcsDiff = nullptr;
    /* Anything but 0 means not a repository, not tracked, or git failed.
     * All three mean the same thing here: no marks, no complaint. A
     * file outside git is the common case, not an error. */
    if (exitCode == 0) {
        m_vcsDiff = ase_vcs_diff_parse(m_vcsOutput.constData(),
                                        static_cast<size_t>(m_vcsOutput.size()));
    }
    /* What the marks were measured against. */
    m_vcsLineCount = static_cast<int>(m_lineStarts.size());
    m_vcsOutput.clear();
    update();
}

AseVcsLineStatus EditorViewport::vcsStatusForLine(int line) const {
    if (m_vcsDiff == nullptr) {
        return ASE_VCS_UNCHANGED;
    }
    /* git diffed the file on disk, so the marks describe the saved
     * version. An unsaved edit that adds or removes a line shifts every
     * line below it and the marks would point at the wrong rows — a mark
     * in the wrong place is worse than no mark, so they go until the
     * next save. Editing within a line moves nothing, and those stay. */
    if (static_cast<int>(m_lineStarts.size()) != m_vcsLineCount) {
        return ASE_VCS_UNCHANGED;
    }
    return ase_vcs_diff_status(m_vcsDiff, line);
}

/* Derived from the theme rather than named outright, so the marks stay
 * coherent with whatever background the user set. Green and red are the
 * one place this editor spends colour on something other than syntax,
 * because "added" and "removed" have no other vocabulary. */
QColor EditorViewport::colorForVcsStatus(AseVcsLineStatus status) const {
    QColor c;
    switch (status) {
    case ASE_VCS_ADDED:
        c = QColor(0x68, 0x9d, 0x6a); /* the same green as syntax_type */
        break;
    case ASE_VCS_MODIFIED:
        c = QColor(0xd7, 0x99, 0x21); /* the same amber as syntax_string */
        break;
    case ASE_VCS_DELETED:
        c = QColor(0xe0, 0x6c, 0x75); /* the diagnostic-error red */
        break;
    default:
        return QColor(Qt::transparent);
    }
    /* Present, not loud: the text is the focus. */
    c.setAlpha(190);
    return c;
}
