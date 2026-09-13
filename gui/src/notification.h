#ifndef ASE_NOTIFICATION_H
#define ASE_NOTIFICATION_H

/*
 * How the editor tells you something — see docs/adr/0062.
 *
 * Until this existed there was no way for the app to say anything at
 * all: an unconfigured language server, a `:` command that doesn't
 * exist, a plugin that failed to load were all *silence*, which reads
 * as "broken" rather than as "not set up". That is exactly how a tester
 * concluded LSP didn't work in the packages.
 *
 * The rule this header exists to enforce: **everything the editor says
 * goes through EditorViewport::notify().** One funnel, one surface, one
 * place to change the rendering. Three ways of saying things becomes six
 * within a month, all looking slightly different — the same failure
 * docs/adr/0053 fixed for animation durations.
 *
 * Note what this is *not* for: continuously-true state ("a language
 * server is running", "a build is in flight"). A message fires once and
 * is gone, so anyone who looks away can never find the answer again.
 * State belongs in a persistent status-bar segment; this is for things
 * that *happen*.
 */
enum class NotifyLevel {
    /* Confirmation of something the user just did. Fades on its own. */
    Info,
    /* Something didn't happen, and the user should know why — an unknown
     * command, a search with no matches. Fades, but sits longer. */
    Warning,
    /* Something failed. Rendered in the theme's existing
     * `diagnostic_error` colour (no new hue enters the palette — see
     * docs/adr/0007) and stays until replaced. */
    Error,
};

#endif /* ASE_NOTIFICATION_H */
