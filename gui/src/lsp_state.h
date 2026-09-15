#ifndef ASE_LSP_STATE_H
#define ASE_LSP_STATE_H

/* Shared by the viewport that shows it and the registry that owns the
 * server it describes. */
enum class LspState {
    NotApplicable, /* not a file we'd start a server for; shows nothing */
    Unconfigured,  /* no server configured for this language */
    Starting,      /* spawned; the initialize handshake is in flight */
    Running,
    Failed,        /* never came up: missing binary, or handshake timeout */
    Stopped,       /* came up, then went away */
};

#endif /* ASE_LSP_STATE_H */
