; Hand-authored, deliberately minimal — see docs/adr/0007, decision 4.
; Only the captures the default theme (ADR 0007, decision 1) actually uses.

[
  "break" "case" "const" "continue" "default" "do" "else" "enum" "extern"
  "for" "goto" "if" "inline" "register" "return" "sizeof" "static"
  "struct" "switch" "typedef" "union" "volatile" "while"
] @keyword

(comment) @comment

(string_literal) @string
(char_literal) @string
(system_lib_string) @string

(number_literal) @number

(primitive_type) @type
(type_identifier) @type
