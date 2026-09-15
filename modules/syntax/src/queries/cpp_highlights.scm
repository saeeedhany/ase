; Hand-authored, minimal — same rule as the C query. Every name checked
; against the grammar's node-types.json; see docs/adr/0085.

[
  "break" "case" "const" "continue" "default" "do" "else" "enum" "extern"
  "for" "goto" "if" "inline" "register" "return" "sizeof" "static"
  "struct" "switch" "typedef" "union" "volatile" "while"

  "catch" "class" "co_await" "co_return" "co_yield" "concept" "constexpr"
  "decltype" "delete" "explicit" "final" "friend" "mutable" "namespace"
  "new" "noexcept" "operator" "override" "private" "protected" "public"
  "requires" "template" "throw" "try" "typename" "using" "virtual"

  "nullptr"
] @keyword

(this) @keyword

(comment) @comment

(string_literal) @string
(raw_string_literal) @string
(char_literal) @string
(system_lib_string) @string

(number_literal) @number

(primitive_type) @type
(type_identifier) @type
(auto) @type
