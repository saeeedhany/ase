#include "lsp_workspace_edit.h"

#include <QUrl>

namespace lsp {
namespace {

/* One TextEdit, or false for anything that cannot be represented
 * faithfully. */
bool oneEdit(const AseJsonValue *entry, const QString &path, const QByteArray &expected,
             project::Replacement *out) {
    if (entry == nullptr || ase_json_type(entry) != ASE_JSON_OBJECT) {
        return false;
    }
    const AseJsonValue *range = ase_json_object_get(entry, "range");
    const char *newText = ase_json_get_string(ase_json_object_get(entry, "newText"));
    if (range == nullptr || newText == nullptr) {
        return false;
    }
    const AseJsonValue *start = ase_json_object_get(range, "start");
    const AseJsonValue *end = ase_json_object_get(range, "end");
    if (start == nullptr || end == nullptr) {
        return false;
    }
    int startLine = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "line"), 0));
    int endLine = static_cast<int>(ase_json_get_number(ase_json_object_get(end, "line"), 0));
    if (startLine != endLine) {
        return false;
    }
    int startChar = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "character"), 0));
    int endChar = static_cast<int>(ase_json_get_number(ase_json_object_get(end, "character"), 0));
    if (endChar <= startChar) {
        return false;
    }

    out->hit.path = path;
    out->hit.line = startLine + 1;
    out->hit.column = startChar + 1;
    out->length = endChar - startChar;
    out->replacement = QByteArray(newText);
    /* The server measures in UTF-16 units and this reads bytes; where
     * they disagree the length is wrong, and a wrong length eats
     * neighbouring text. Naming what must be there turns that into a
     * skipped edit rather than a mangled line. */
    out->expected = expected;
    out->accepted = true;
    return true;
}

} // namespace

QVector<project::Replacement> replacementsFrom(const AseJsonValue *workspaceEdit, const QDir &root,
                                                const QByteArray &expected) {
    QVector<project::Replacement> out;
    if (workspaceEdit == nullptr || ase_json_type(workspaceEdit) != ASE_JSON_OBJECT) {
        return out;
    }

    auto collect = [&](const char *uri, const AseJsonValue *edits) {
        if (uri == nullptr || edits == nullptr || ase_json_type(edits) != ASE_JSON_ARRAY) {
            return;
        }
        const QString local = QUrl(QString::fromUtf8(uri)).toLocalFile();
        if (local.isEmpty()) {
            return; /* a built-in, or something inside an archive */
        }
        const QString relative = root.relativeFilePath(local);
        for (size_t i = 0; i < ase_json_array_size(edits); i++) {
            project::Replacement item;
            if (oneEdit(ase_json_array_get(edits, i), relative, expected, &item)) {
                out.push_back(item);
            }
        }
    };

    /* `changes` is an object keyed by URI; `documentChanges` is an array
     * of {textDocument, edits} carrying the version it was computed
     * against. Servers pick, so both are read. */
    const AseJsonValue *changes = ase_json_object_get(workspaceEdit, "changes");
    if (changes != nullptr && ase_json_type(changes) == ASE_JSON_OBJECT) {
        for (size_t i = 0; i < ase_json_object_size(changes); i++) {
            collect(ase_json_object_key(changes, i), ase_json_object_value(changes, i));
        }
        return out;
    }

    const AseJsonValue *documentChanges = ase_json_object_get(workspaceEdit, "documentChanges");
    if (documentChanges != nullptr && ase_json_type(documentChanges) == ASE_JSON_ARRAY) {
        for (size_t i = 0; i < ase_json_array_size(documentChanges); i++) {
            const AseJsonValue *change = ase_json_array_get(documentChanges, i);
            if (change == nullptr || ase_json_type(change) != ASE_JSON_OBJECT) {
                continue;
            }
            const AseJsonValue *doc = ase_json_object_get(change, "textDocument");
            collect(doc != nullptr ? ase_json_get_string(ase_json_object_get(doc, "uri")) : nullptr,
                    ase_json_object_get(change, "edits"));
        }
    }
    return out;
}

} // namespace lsp
