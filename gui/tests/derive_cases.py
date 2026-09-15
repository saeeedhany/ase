#!/usr/bin/env python3
"""Regenerate vim_cases.inc by running each case through real vim.

The .inc file is committed so the suite needs no vim to run; this script
exists so the expectations can be re-derived and audited rather than
trusted. Run it from the repository root:

    python3 gui/tests/derive_cases.py

`vim -u NONE -i NONE` keeps viminfo out (it carries registers between
runs and makes `p` paste something never yanked), and `nofixeol` stops
vim appending a trailing newline that makes identical results differ.
"""
import os, shutil, subprocess, sys, tempfile

# (name, input, line, column, keys) — column is absolute, not first-non-blank.
CASES = [
    # --- delete, change, counts ---
    ("x",                 "abc\n",                    1, 1, "x"),
    ("3x",                "abcdef\n",                 1, 1, "3x"),
    ("dd",                "aaa\nbbb\nccc\nddd\n",     2, 1, "dd"),
    ("2dd",               "aaa\nbbb\nccc\nddd\n",     2, 1, "2dd"),
    ("dj",                "aaa\nbbb\nccc\nddd\n",     2, 1, "dj"),
    ("dk",                "aaa\nbbb\nccc\nddd\n",     3, 1, "dk"),
    ("dG",                "aaa\nbbb\nccc\nddd\n",     2, 1, "dG"),
    ("dgg",               "aaa\nbbb\nccc\nddd\n",     3, 1, "dgg"),

    # A counted D starting at column 1 takes the line; elsewhere it keeps it.
    ("D",                 "aaa\nbbb\nccc\nddd\n",     2, 1, "D"),
    ("2D col 1",          "aaa\nbbb\nccc\nddd\n",     2, 1, "2D"),
    ("2D col 2",          "aaa\nbbb\nccc\nddd\n",     2, 2, "2D"),
    ("2D col 3",          "aaa\nbbb\nccc\nddd\n",     2, 3, "2D"),
    ("3D",                "aaa\nbbb\nccc\nddd\n",     2, 1, "3D"),
    ("C",                 "aaa\nbbb\nccc\nddd\n",     2, 1, "CZZ<Esc>"),
    ("2C",                "aaa\nbbb\nccc\nddd\n",     2, 1, "2CZZ<Esc>"),

    # `w` under an operator stops at a line end; a charwise delete
    # covering whole lines becomes linewise.
    ("dw at line end",    "aaa\nbbb\nccc\nddd\n",     2, 1, "dw"),
    ("2dw whole lines",   "aaa\nbbb\nccc\nddd\n",     2, 1, "2dw"),
    ("3dw",               "aaa\nbbb\nccc\nddd\n",     2, 1, "3dw"),
    ("dw mid-line",       "aaa bbb\nccc ddd\neee\n",  1, 1, "dw"),
    ("2dw mid-line",      "aaa bbb\nccc ddd\neee\n",  1, 1, "2dw"),
    ("3dw mid-line",      "aaa bbb\nccc ddd\neee\n",  1, 1, "3dw"),
    ("4dw mid-line",      "aaa bbb\nccc ddd\neee\n",  1, 1, "4dw"),
    ("cw",                "aaa bbb\nccc ddd\neee\n",  1, 1, "cwZZ<Esc>"),
    ("de",                "foo.bar baz\n",            1, 1, "de"),

    # h and l stay on their line, where the arrow keys they share a
    # primitive with wrap.
    ("d10l clamps",       "abc\ndefgh\nij\n",         2, 1, "d10l"),
    ("10l then x",        "abc\ndefgh\nij\n",         2, 1, "10lx"),
    ("10h then x",        "abc\ndefgh\nij\n",         2, 3, "10hx"),

    # --- yank, paste, undo ---
    ("yyp",               "aaa\nbbb\nccc\nddd\n",     2, 1, "yyp"),
    ("yyP",               "aaa\nbbb\nccc\nddd\n",     2, 1, "yyP"),
    ("ywP",               "aaa bbb\nccc ddd\neee\n",  1, 1, "ywP"),
    ("x then u",          "abc\n",                    1, 1, "xu"),
    ("dd then u",         "aaa\nbbb\nccc\n",          2, 1, "ddu"),
    ("2dw then u",        "aaa bbb\nccc ddd\n",       1, 1, "2dwu"),

    # --- open, insert, replace ---
    ("o",                 "aaa\nbbb\n",               1, 1, "oNEW<Esc>"),
    ("O",                 "aaa\nbbb\n",               2, 1, "ONEW<Esc>"),
    ("cc",                "aaa\nbbb\nccc\n",          2, 1, "ccZZ<Esc>"),
    ("r",                 "abc\n",                    1, 1, "rZ"),
    ("3r",                "abcdef\n",                 1, 1, "3rZ"),
    ("R",                 "abcdef\n",                 1, 1, "RXY<Esc>"),
    ("s",                 "abc\n",                    1, 1, "sZZ<Esc>"),
    ("S",                 "aaa\nbbb\n",               1, 1, "SZZ<Esc>"),
    ("X",                 "abc\n",                    1, 3, "X"),

    # --- join ---
    ("J",                 "aaa\nbbb\nccc\n",          1, 1, "J"),
    ("3J",                "aaa\nbbb\nccc\nddd\n",     1, 1, "3J"),
    ("J trailing blank",  "ee \n   fff\n",            1, 1, "J"),
    ("J before paren",    "gg\n)hh\n",                1, 1, "J"),
    ("J keeps blanks",    "kk   \nll\n",              1, 1, "J"),
    ("J with empty line", "ii\n\njj\n",               1, 1, "J"),
    ("J on last line",    "aa\nbb\n",                 2, 1, "J"),
    ("gJ",                "aaa\nbbb\n",               1, 1, "gJ"),
    ("gJ keeps indent",   "ee \n   fff\n",            1, 1, "gJ"),
    ("visual J",          "aaa\nbbb\nccc\n",          1, 1, "VjJ"),

    # --- text objects ---
    ("diw",               "one  two three\n",         1, 1, "diw"),
    ("diw on blanks",     "one  two three\n",         1, 4, "diw"),
    ("diw second word",   "one  two three\n",         1, 6, "diw"),
    ("daw",               "one  two three\n",         1, 1, "daw"),
    ("daw second word",   "one  two three\n",         1, 6, "daw"),
    ("daw last word",     "one  two three\n",         1, 11, "daw"),
    ("diW",               "foo.bar baz\n",            1, 1, "diW"),
    ("daW",               "foo.bar baz\n",            1, 1, "daW"),
    ("diw punctuation",   "foo.bar baz\n",            1, 4, "diw"),
    ("di(",               "foo(bar, baz) end\n",      1, 6, "di("),
    ("da(",               "foo(bar, baz) end\n",      1, 6, "da("),
    ("di( before pair",   "foo(bar, baz) end\n",      1, 1, "di("),
    ("di( after pair",    "foo(bar, baz) end\n",      1, 15, "di("),
    ("di( nested",        "a(b(c)d) e\n",             1, 4, "di("),
    ("da( nested",        "a(b(c)d) e\n",             1, 4, "da("),
    ("di{",               "key {val} tail\n",         1, 7, "di{"),
    ("da{",               "key {val} tail\n",         1, 7, "da{"),
    ("di[",               "list [one two] tail\n",    1, 8, "di["),
    ("di<",               "p <tag> q\n",              1, 4, "di<"),
    ('di"',               'say "hi there" now\n',     1, 7, 'di"'),
    ('da"',               'say "hi there" now\n',     1, 7, 'da"'),
    ("di'",               "it 'quoted' up\n",         1, 6, "di'"),
    ("vi(d",              "a(b(c)d) e\n",             1, 4, "vi(d"),
    ("viwd",              "foo.bar baz\n",            1, 1, "viwd"),
    ("ci( single line",   "foo(bar) end\n",           1, 6, "ci(Z<Esc>"),
    ("di{ whole lines",   "C {\n d\n} D\n",           2, 2, "di{"),
    ("di{ brace mid-line","A { a\n b } B\n",          1, 5, "di{"),
    ("ci{ whole lines",   "C {\n d\n} D\n",           2, 2, "ci{Z<Esc>"),

    # --- marks ---
    ("mark backtick",     "one\ntwo\n    three\n",    3, 9, "majjd`a"),
    ("d'a linewise",      "one\ntwo\nthree\nfour\n",  2, 2, "ma2jd'a"),
    ("d`a charwise",      "one\ntwo\nthree\nfour\n",  2, 2, "ma2jd`a"),
    ("c'a",               "one\ntwo\nthree\nfour\n",  2, 2, "ma2jc'aZZ<Esc>"),
    ("d'a unset mark",    "one\ntwo\nthree\n",        2, 1, "d'z"),

    # --- macros ---
    # Recording cannot be derived: vim refuses to record inside :normal,
    # so `@a` comes back empty and the file is unchanged. These are in
    # MANUAL_CASES below with the reasoning instead.

    # --- named registers ---
    # `"` was unhandled before: the letter after it entered Insert and the
    # rest of the command was typed into the buffer.
    ("reg yank and paste",  "aaa\nbbb\nccc\nddd\n", 1, 1, '"ayyj"ap'),
    ("reg delete and paste","aaa\nbbb\nccc\nddd\n", 1, 1, '"addj"ap'),
    ("reg append uppercase","aaa\nbbb\nccc\nddd\n", 1, 1, '"ayyj"Ayyj"ap'),
    ("reg also fills unnamed","aaa\nbbb\nccc\nddd\n", 1, 1, '"ayyjp'),
    ("reg charwise",        "aaa\nbbb\nccc\nddd\n", 1, 1, '"aywj"aP'),
    ("reg two registers",   "aaa\nbbb\nccc\nddd\n", 1, 1, '"ayyj"byyj"ap"bp'),
    ("reg survives motion", "aaa\nbbb\nccc\nddd\n", 1, 1, '"ayyGk"ap'),
    ("reg visual yank",     "aaa\nbbb\nccc\nddd\n", 1, 1, '"aVyj"ap'),

    # --- dot repeat ---
    ("dot repeat x",      "aaaa\nbbbb\ncccc\n",       1, 1, "2xj0."),
    ("dot repeat dw",     "aa bb\ncc dd\n",           1, 1, "dwj0."),
]


# Cases real vim cannot be scripted into producing, with the reason and
# the behaviour each one pins down. Verified by hand against interactive
# vim; see docs/adr/0104.
MANUAL_CASES = [
    # `qaxjq` records x then j; `@a` replays both, so line 2 loses a
    # character too. vim -es cannot record, so this cannot be derived.
    ("macro record+play", "alpha\nbravo\ncharlie\n", 1, 1, "qaxjq@a",
     "lpha\nravo\ncharlie\n"),
    ("macro with count",  "aa\nbb\ncc\ndd\n", 1, 1, "qaxjq2@a",
     "a\nb\nc\ndd\n"),
    ("macro @@",          "aa\nbb\ncc\ndd\n", 1, 1, "qaxjq@a@@",
     "a\nb\nc\ndd\n"),
    # A file with no trailing newline: our buffer terminates the last
    # line where vim's does not, which only shows with 'nofixeol'. vim's
    # own default (fixeol) writes the same bytes we do.
    ("yyp without trailing newline", "a\nb", 2, 1, "yyp", "a\nb\nb\n"),
]


# `:` commands go through runCommand rather than key events, so they get
# their own table. Derived the same way: vim runs the ex command.
COMMAND_CASES = [
    ("s first match",     "foo bar foo\nbaz foo qux\n",           1, 1, "s/foo/X/"),
    ("s global",          "foo bar foo\nbaz foo qux\n",           1, 1, "s/foo/X/g"),
    ("s whole file",      "foo bar foo\nbaz foo qux\nFOO up\n",   1, 1, "%s/foo/X/g"),
    ("s line range",      "aa\nfoo\nfoo\nfoo\n",                 1, 1, "2,3s/foo/X/"),
    ("s to last line",    "aa\nfoo\nfoo\nfoo\n",                 1, 1, "2,$s/foo/X/"),
    ("s relative range",  "foo\nfoo\nfoo\nfoo\n",                2, 1, ".,+1s/foo/X/"),
    ("s ignore case",     "foo bar\nFOO baz\n",                   2, 1, "s/foo/X/i"),
    ("s alternate sep",   "a/b/c\n",                              1, 1, "s#/#-#g"),
    ("s ampersand",       "foo bar\n",                            1, 1, "s/foo/[&]/"),
    ("s group backref",   "foo bar\n",                            1, 1, r"s/\(f\)oo/<\1>/"),
    ("s two groups",      "foo bar\n",                            1, 1, r"s/\(f\)\(oo\)/\2\1/"),
    ("s bare parens",     "a(b)c plain\n",                        1, 1, "s/(b)/[&]/"),
    ("s literal plus",    "xx+yy\n",                              1, 1, "s/x+/Q/"),
    ("s one or more",     "xx+yy\n",                              1, 1, r"s/x\+/Q/"),
    ("s word boundary",   "foo food foo\n",                       1, 1, r"%s/\<foo\>/W/g"),
    ("s alternation",     "foo bar\n",                            1, 1, r"s/f\|b/Z/g"),
    ("s no match",        "foo bar\n",                            1, 1, "s/zzz/Q/"),
    ("s empty line range","aa\nfoo\n\nfoo\n",                    1, 1, "%s/foo/X/g"),
    ("goto line",         "aa\nbb\ncc\n",                         1, 1, "2"),
]

VIM_KEY = {"<Esc>": r"\<Esc>", "<CR>": r"\<CR>", "<BS>": r"\<BS>", "<Tab>": r"\<Tab>"}


def to_vim(keys):
    out = keys
    for k, v in VIM_KEY.items():
        out = out.replace(k, v)
    return out.replace('"', r'\"')


def c_string(text):
    out = []
    for ch in text:
        out.append({"\n": "\\n", "\t": "\\t", '"': '\\"', "\\": "\\\\"}.get(ch, ch))
    return '"' + "".join(out) + '"'


def main():
    if shutil.which("vim") is None:
        print("vim not found; cannot derive expectations", file=sys.stderr)
        return 1
    work = tempfile.mkdtemp()
    rows = []
    for name, text, line, col, keys in CASES:
        open(os.path.join(work, "in.txt"), "w").write(text)
        script = (
            "set nofixeol\n"
            "call cursor(%d,%d)\n"
            'execute "normal %s"\n'
            "write! %s/out.txt\nq!\n" % (line, col, to_vim(keys), work)
        )
        open(os.path.join(work, "case.vim"), "w").write(script)
        subprocess.run(["vim", "-u", "NONE", "-i", "NONE", "-N", "-es", "-S", "case.vim", "in.txt"],
                       cwd=work, capture_output=True)
        expected = open(os.path.join(work, "out.txt")).read()
        rows.append((name, text, line, col, keys, expected))

    cmd_rows = []
    for name, text, line, col, command in COMMAND_CASES:
        open(os.path.join(work, "in.txt"), "w").write(text)
        script = ("set nofixeol\ncall cursor(%d,%d)\ntry\n  %s\ncatch\nendtry\n"
                  "write! %s/out.txt\nq!\n" % (line, col, command, work))
        open(os.path.join(work, "case.vim"), "w").write(script)
        subprocess.run(["vim", "-u", "NONE", "-i", "NONE", "-N", "-es", "-S", "case.vim", "in.txt"],
                       cwd=work, capture_output=True)
        cmd_rows.append((name, text, line, col, command, open(os.path.join(work, "out.txt")).read()))

    header = (
        "/* Generated by gui/tests/derive_cases.py — every expectation below\n"
        " * is what real vim produced for those keys. Committed so the suite\n"
        " * runs without vim; regenerate to audit or extend. */\n\n"
    )
    with open("gui/tests/vim_cases.inc", "w") as out:
        out.write(header)
        for name, text, line, col, keys, expected in rows + MANUAL_CASES:
            out.write("{%s, %s, %d, %d, %s, %s},\n" % (
                c_string(name), c_string(text), line, col, c_string(keys), c_string(expected)))
    with open("gui/tests/vim_command_cases.inc", "w") as out:
        out.write(header.replace("keys", "commands"))
        for name, text, line, col, command, expected in cmd_rows:
            out.write("{%s, %s, %d, %d, %s, %s},\n" % (
                c_string(name), c_string(text), line, col, c_string(command), c_string(expected)))
    print("wrote %d key cases (+%d manual) and %d command cases"
          % (len(rows), len(MANUAL_CASES), len(cmd_rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
