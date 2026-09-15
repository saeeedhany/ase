; Hand-authored, minimal. Every name checked against the grammar's
; node-types.json; see docs/adr/0103.

[
  "as" "async" "await" "break" "case" "catch" "class" "const" "continue"
  "debugger" "default" "delete" "do" "else" "export" "extends" "finally"
  "for" "from" "function" "get" "if" "import" "in" "instanceof" "let"
  "new" "of" "return" "set" "static" "switch" "throw" "try" "typeof"
  "var" "void" "while" "with" "yield"
] @keyword

(true) @keyword
(false) @keyword
(null) @keyword
(undefined) @keyword

(comment) @comment
(string) @string
(template_string) @string
(regex) @string
(number) @number
