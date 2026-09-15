; Hand-authored, minimal — same rule as the C query. Every name checked
; against the grammar's node-types.json; see docs/adr/0103.

[
  "and" "as" "assert" "async" "await" "break" "case" "class" "continue"
  "def" "del" "elif" "else" "except" "finally" "for" "from" "global"
  "if" "import" "in" "is" "lambda" "match" "nonlocal" "not" "or" "pass"
  "raise" "return" "try" "while" "with" "yield"
] @keyword

(true) @keyword
(false) @keyword
(none) @keyword

(comment) @comment
(string) @string
(integer) @number
(float) @number
(type) @type
