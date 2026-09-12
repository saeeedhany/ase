#ifndef ASE_EDITOR_VIEWPORT_INTERNAL_H
#define ASE_EDITOR_VIEWPORT_INTERNAL_H

/*
 * Private to EditorViewport's own translation units — see docs/adr/0052
 * for why that one class is spread across several .cpp files at all.
 * Nothing outside gui/src/editor_viewport*.cpp should include this.
 *
 * Deliberately minimal: only the handful of constants and byte helpers
 * that genuinely have callers in more than one of those files lives
 * here. Everything else stays in its own file's anonymous namespace,
 * which is the useful signal — a constant that needs promoting to this
 * header is a hint that two files are reaching into the same concern.
 */

/* Caret/cursor width in pixels. Shared because the renderer draws with
 * it and ensureCursorVisible() reserves horizontal scroll margin for
 * it. */
constexpr int kCaretWidth = 2;

/* Typing pop-in duration — 4 ticks of motion::kTickMs (~120ms),
 * short enough to read as a snappy "just landed" pop rather than a
 * sluggish delay before typed text looks finished. See docs/adr/0049.
 * Shared because insertText() stamps entries with it and the renderer
 * measures their progress against it. */
constexpr int kTypingAnimationTicks = 4;

/* A UTF-8 continuation byte is 10xxxxxx — i.e. not the first byte of a
 * codepoint. Every "step one character, not one byte" walk in this
 * class (the renderer's run measurement, Vim's motions, Backspace)
 * keys off this. */
inline bool isUtf8ContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

/* ASCII-only word/identifier byte, deliberately: the same simplification
 * the byte-level column model already makes (docs/adr/0012). Shared by
 * Ctrl+D's whole-word match, Vim's word motions, and the completion
 * prefix scan. */
inline bool isWordChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9') || uc == '_';
}

#endif /* ASE_EDITOR_VIEWPORT_INTERNAL_H */
