# ADR 0102: `:s`, and the regex dialect it speaks

## Status

Accepted

## Context

[ADR 0073](0073-the-command-line-moves-to-the-status-bar.md) and
[ADR 0074](0074-search-with-slash-and-n.md) both justified keeping
Find/Replace as a floating panel with the same sentence: *"vim's own
answer to replace is `:s/a/b/` — through the command line anyway."*

`:s` did not exist. The division those ADRs describe was real, but one
side of it was a promise.

## Decision

### The split stands; `:s` fills its half

Nothing about the floating Find/Replace changes. `Ctrl+F` and `Ctrl+H`
still open the two-field panel, and it remains the way to do this
without vim — confirmed working with `vim_mode = false`, which is the
profile it exists for.

The division is by shape, exactly as ADR 0073 set it out:

- **status-bar line** — `:` commands, `/` `?` search, and now `:s`: one
  field, typed as a whole
- **floating panel** — find *and* replace: two fields, edited
  independently

`:s` works with vim mode off too, since `Ctrl+;` opens the command line
either way. It is not a replacement for the panel and does not try to be.

### Supported

`:[range]s<sep>pattern<sep>replacement<sep>[flags]`, where the separator
is whatever non-alphanumeric character follows the `s` — `:s#a#b#` works
because paths contain slashes.

Ranges: none (this line), `%`, a number, `.`, `$`, `+N`, `-N`, and any
pair of those around a comma. Flags: `g` for every match on a line, `i`
for case-insensitive. Replacement: `&` for the whole match, `\1`..`\9`
for groups, `\&` for a literal ampersand.

### Patterns are vim's dialect, translated

This is the decision worth recording. Qt gives PCRE, and PCRE is not
what `:s` speaks — in vim's default *magic* level, `\(` groups and a
bare `(` is literal, which is exactly backwards from PCRE. A pattern as
ordinary as `\(f\)oo` would have meant something else.

So the pattern is translated before it reaches `QRegularExpression`:
`( ) | + ? { }` swap their escaping, `\=` becomes `?`, and `\<` `\>`
become `\b`. `.` `*` `[` `]` `^` `$` already agree and pass through.

Checked against vim, same file and same command:

| pattern | |
|---|---|
| `s/\(f\)oo/<\1>/g` | group and backreference |
| `s/(b)/[&]/` | bare parens are literal |
| `s/x+/Q/` vs `s/x\+/Q/` | literal `+` vs one-or-more |
| `s/\<foo\>/W/g` | word boundaries |
| `s/f\|b/Z/g` | alternation |
| `s/\(f\)\(oo\)/\2\1/` | two groups, swapped |

All six match. Anything PCRE understands that vim also spells the same
way — character classes, anchors, lazy quantifiers — comes along for
free; anything vim spells differently and is not in that list does not.

### The replacement is expanded by hand

`QString::replace` knows `\1`..`\9` but has no spelling for the whole
match, and `&` is the one every `:s` uses. So matches are iterated and
the replacement built directly, which also makes zero-width matches
(`s/x*/-/g`) terminate instead of looping.

## Consequences

19 comparisons against vim match: the basic forms, `g` and `i`, `%`,
`N,M`, `2,$`, `.,+1`, an alternate separator, `&`, and the six patterns
above. A whole-file substitute is one undo step — `:%s/foo/Q/g` then `u`
restores the file byte for byte — and the message matches vim's wording,
"5 substitutions on 3 lines".

Not supported: `c` (confirm), an empty pattern meaning "the last search",
`~` for the previous replacement, and a bare `:s` repeating the last
substitution. Each needs state `:s` does not keep yet.

`:s` is recognised by finding the first `s` whose next character is not
alphanumeric, so `:sort` still reports "unknown command" rather than
being read as a substitution with `o` for a separator. A plugin command
of that shape would collide; none does.
