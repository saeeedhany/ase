#include "ase/buffer.h"

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PIECE_SOURCE_ORIGINAL,
    PIECE_SOURCE_ADD,
} PieceSource;

typedef struct Piece {
    PieceSource source;
    size_t start;
    size_t length;
    struct Piece *prev;
    struct Piece *next;
} Piece;

struct AseBuffer {
    char *original;
    size_t original_len;

    char *add;
    size_t add_len;
    size_t add_cap;

    Piece *head;
    Piece *tail;
    size_t piece_count;
    size_t total_length;

    /* Last-access cache: purely a performance optimization, not part of
     * logical state, so const readers (get_text, line_count) are allowed
     * to update it via a cast. See docs/adr/0005. */
    Piece *cache_piece;
    size_t cache_piece_start;
};

static Piece *piece_create(PieceSource source, size_t start, size_t length) {
    Piece *p = (Piece *)malloc(sizeof(Piece));
    if (p == NULL) {
        return NULL;
    }
    p->source = source;
    p->start = start;
    p->length = length;
    p->prev = NULL;
    p->next = NULL;
    return p;
}

/* Shrinks `p` in place to its first `split_offset` bytes and returns a new
 * piece holding the remainder, unlinked. Caller must splice it in. */
static Piece *piece_split(Piece *p, size_t split_offset) {
    Piece *right = piece_create(p->source, p->start + split_offset, p->length - split_offset);
    if (right == NULL) {
        return NULL;
    }
    p->length = split_offset;
    return right;
}

static void list_append(AseBuffer *buf, Piece *p) {
    p->prev = buf->tail;
    p->next = NULL;
    if (buf->tail != NULL) {
        buf->tail->next = p;
    } else {
        buf->head = p;
    }
    buf->tail = p;
    buf->piece_count++;
}

static void list_insert_after(AseBuffer *buf, Piece *ref, Piece *p) {
    p->prev = ref;
    p->next = ref->next;
    if (ref->next != NULL) {
        ref->next->prev = p;
    } else {
        buf->tail = p;
    }
    ref->next = p;
    buf->piece_count++;
}

static void list_insert_before(AseBuffer *buf, Piece *ref, Piece *p) {
    p->next = ref;
    p->prev = ref->prev;
    if (ref->prev != NULL) {
        ref->prev->next = p;
    } else {
        buf->head = p;
    }
    ref->prev = p;
    buf->piece_count++;
}

/* Replaces `old` with `replacement` in the list. Piece count is unchanged
 * (one node out, one node in) — caller owns freeing `old`. */
static void list_replace(AseBuffer *buf, Piece *old, Piece *replacement) {
    replacement->prev = old->prev;
    replacement->next = old->next;
    if (old->prev != NULL) {
        old->prev->next = replacement;
    } else {
        buf->head = replacement;
    }
    if (old->next != NULL) {
        old->next->prev = replacement;
    } else {
        buf->tail = replacement;
    }
}

static void list_unlink(AseBuffer *buf, Piece *p) {
    if (p->prev != NULL) {
        p->prev->next = p->next;
    } else {
        buf->head = p->next;
    }
    if (p->next != NULL) {
        p->next->prev = p->prev;
    } else {
        buf->tail = p->prev;
    }
    buf->piece_count--;
}

/* Finds the piece containing byte offset `offset` (0 <= offset <=
 * buf->total_length). Returns NULL with *start_out == buf->total_length
 * when offset lands exactly at the end of the buffer ("insert/append
 * here"). Updates the access cache as a side effect. */
static Piece *locate(AseBuffer *buf, size_t offset, size_t *start_out) {
    Piece *piece;
    size_t start;

    if (buf->cache_piece != NULL) {
        piece = buf->cache_piece;
        start = buf->cache_piece_start;
    } else {
        piece = buf->head;
        start = 0;
    }

    while (start > offset) {
        piece = piece->prev;
        start -= piece->length;
    }

    while (piece != NULL && offset >= start + piece->length) {
        start += piece->length;
        piece = piece->next;
    }

    buf->cache_piece = piece;
    buf->cache_piece_start = start;
    *start_out = start;
    return piece;
}

static bool add_buffer_append(AseBuffer *buf, const char *text, size_t len, size_t *start_out) {
    if (buf->add_len + len > buf->add_cap) {
        size_t new_cap = buf->add_cap == 0 ? 64 : buf->add_cap;
        while (new_cap < buf->add_len + len) {
            new_cap *= 2;
        }
        char *grown = (char *)realloc(buf->add, new_cap);
        if (grown == NULL) {
            return false;
        }
        buf->add = grown;
        buf->add_cap = new_cap;
    }
    memcpy(buf->add + buf->add_len, text, len);
    *start_out = buf->add_len;
    buf->add_len += len;
    return true;
}

AseBuffer *ase_buffer_create(void) {
    return (AseBuffer *)calloc(1, sizeof(AseBuffer));
}

AseBuffer *ase_buffer_create_from_file(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    AseBuffer *buf = (AseBuffer *)calloc(1, sizeof(AseBuffer));
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    if (size > 0) {
        buf->original = (char *)malloc((size_t)size);
        if (buf->original == NULL) {
            fclose(f);
            free(buf);
            return NULL;
        }

        size_t read = fread(buf->original, 1, (size_t)size, f);
        fclose(f);
        if (read != (size_t)size) {
            free(buf->original);
            free(buf);
            return NULL;
        }
        buf->original_len = (size_t)size;

        Piece *p = piece_create(PIECE_SOURCE_ORIGINAL, 0, buf->original_len);
        if (p == NULL) {
            free(buf->original);
            free(buf);
            return NULL;
        }
        list_append(buf, p);
        buf->total_length = buf->original_len;
    } else {
        fclose(f);
    }

    return buf;
}

void ase_buffer_destroy(AseBuffer *buf) {
    if (buf == NULL) {
        return;
    }
    Piece *p = buf->head;
    while (p != NULL) {
        Piece *next = p->next;
        free(p);
        p = next;
    }
    free(buf->original);
    free(buf->add);
    free(buf);
}

size_t ase_buffer_length(const AseBuffer *buf) {
    return buf == NULL ? 0 : buf->total_length;
}

size_t ase_buffer_piece_count(const AseBuffer *buf) {
    return buf == NULL ? 0 : buf->piece_count;
}

size_t ase_buffer_line_count(const AseBuffer *buf) {
    if (buf == NULL || buf->total_length == 0) {
        return 1;
    }

    size_t lines = 1;
    for (Piece *p = buf->head; p != NULL; p = p->next) {
        const char *base = (p->source == PIECE_SOURCE_ORIGINAL) ? buf->original : buf->add;
        const char *chunk = base + p->start;
        for (size_t i = 0; i < p->length; i++) {
            if (chunk[i] == '\n') {
                lines++;
            }
        }
    }
    return lines;
}

bool ase_buffer_insert(AseBuffer *buf, size_t at, const char *text, size_t len) {
    if (buf == NULL || (text == NULL && len > 0) || at > buf->total_length) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    size_t add_start;
    if (!add_buffer_append(buf, text, len, &add_start)) {
        return false;
    }

    Piece *new_piece = piece_create(PIECE_SOURCE_ADD, add_start, len);
    if (new_piece == NULL) {
        /* The add-buffer already grew; that space is simply unused. Wasteful
         * but leaves buffer state consistent, and allocation failure paths
         * like this are not the focus of a v1 buffer engine. */
        return false;
    }

    size_t piece_start;
    Piece *at_piece = locate(buf, at, &piece_start);

    if (at_piece == NULL) {
        list_append(buf, new_piece);
    } else if (piece_start == at) {
        list_insert_before(buf, at_piece, new_piece);
    } else {
        size_t split_offset = at - piece_start;
        Piece *right = piece_split(at_piece, split_offset);
        if (right == NULL) {
            free(new_piece);
            return false;
        }
        list_insert_after(buf, at_piece, right);
        list_insert_after(buf, at_piece, new_piece);
    }

    buf->total_length += len;
    buf->cache_piece = new_piece;
    buf->cache_piece_start = at;
    return true;
}

bool ase_buffer_delete(AseBuffer *buf, size_t at, size_t len) {
    if (buf == NULL || at > buf->total_length || len > buf->total_length - at) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    size_t piece_start;
    Piece *piece = locate(buf, at, &piece_start);

    if (piece_start < at) {
        size_t split_offset = at - piece_start;
        Piece *right = piece_split(piece, split_offset);
        if (right == NULL) {
            return false;
        }
        list_insert_after(buf, piece, right);
        piece = right;
    }

    Piece *cache_after = piece->prev;
    size_t remaining = len;

    while (remaining > 0) {
        if (remaining < piece->length) {
            Piece *right = piece_split(piece, remaining);
            if (right == NULL) {
                return false;
            }
            list_replace(buf, piece, right);
            free(piece);
            remaining = 0;
        } else {
            remaining -= piece->length;
            Piece *next = piece->next;
            list_unlink(buf, piece);
            free(piece);
            piece = next;
        }
    }

    buf->total_length -= len;
    buf->cache_piece = cache_after;
    buf->cache_piece_start = (cache_after != NULL) ? at - cache_after->length : 0;
    return true;
}

size_t ase_buffer_get_text(const AseBuffer *buf, size_t at, size_t len, char *out) {
    if (buf == NULL || out == NULL || at > buf->total_length) {
        return 0;
    }
    if (len > buf->total_length - at) {
        len = buf->total_length - at;
    }
    if (len == 0) {
        return 0;
    }

    /* Cache mutation is a logically-const performance optimization; see
     * the AseBuffer::cache_piece comment. */
    AseBuffer *mutable_buf = (AseBuffer *)buf;

    size_t piece_start;
    Piece *piece = locate(mutable_buf, at, &piece_start);
    size_t offset_in_piece = at - piece_start;
    size_t copied = 0;

    while (copied < len && piece != NULL) {
        const char *base = (piece->source == PIECE_SOURCE_ORIGINAL) ? buf->original : buf->add;
        size_t avail = piece->length - offset_in_piece;
        size_t remaining = len - copied;
        size_t take = avail < remaining ? avail : remaining;

        memcpy(out + copied, base + piece->start + offset_in_piece, take);
        copied += take;
        offset_in_piece = 0;
        piece = piece->next;
    }

    return copied;
}

/* Every piece, in order, into an already-open stream. */
static bool buffer_write_pieces(const AseBuffer *buf, FILE *f) {
    for (Piece *p = buf->head; p != NULL; p = p->next) {
        const char *base = (p->source == PIECE_SOURCE_ORIGINAL) ? buf->original : buf->add;
        if (p->length > 0 && fwrite(base + p->start, 1, p->length, f) != p->length) {
            return false;
        }
    }
    return true;
}

/* The old path, kept only for the case below where no temporary file can
 * be created. It truncates before it writes, so a failure part-way
 * destroys what was there. */
static bool buffer_write_in_place(const AseBuffer *buf, const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    if (!buffer_write_pieces(buf, f)) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

/*
 * Writes beside the target, then renames over it. rename() is atomic, so
 * the file on disk is either entirely the old contents or entirely the
 * new ones — never the half that a failed write leaves behind. The
 * "never loses user data" pillar in docs/SPEC.md is the reason; a capped
 * filesystem used to leave a truncated file and an honest "could not
 * write". See docs/adr/0109.
 *
 * The temporary has to live in the same directory: rename() is only
 * atomic within a filesystem, and /tmp is frequently a different one.
 */
bool ase_buffer_save_to_file(const AseBuffer *buf, const char *path) {
    if (buf == NULL || path == NULL) {
        return false;
    }

    /* Through a symlink, not over it: replacing the link is how an
     * editor silently detaches a dotfile from the repository it is
     * checked into. A path that does not exist yet resolves to itself. */
    char *resolved = ase_real_path(path);
    const char *target = (resolved != NULL) ? resolved : path;

    char *tmp = ase_temp_path_beside(target);
    if (tmp == NULL) {
        free(resolved);
        return false;
    }

    FILE *f = ase_open_private(tmp);
    if (f == NULL) {
        /* No temporary is possible — a directory that is not writable
         * while the file itself is. Refusing to save at all would strand
         * the user's work, so this falls back to the unsafe write rather
         * than to nothing. It is the one path that can still lose data. */
        free(tmp);
        bool ok = buffer_write_in_place(buf, target);
        free(resolved);
        return ok;
    }

    bool ok = buffer_write_pieces(buf, f);
    if (ok) {
        ok = (fflush(f) == 0) && ase_fsync_stream(f);
    }
    if (fclose(f) != 0) {
        ok = false;
    }

    /* Before the rename, so the file is never briefly world-readable. */
    if (ok) {
        ase_copy_permissions(target, tmp);
    }

    if (!ok || !ase_rename_over(tmp, target)) {
        remove(tmp);
        free(tmp);
        free(resolved);
        return false;
    }

    /* The rename itself is only durable once the directory entry is. */
    ase_fsync_parent_directory(target);

    free(tmp);
    free(resolved);
    return true;
}
