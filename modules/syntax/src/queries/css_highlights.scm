; Hand-authored, minimal. CSS has no keywords to speak of, so the useful
; split is property names against selectors. Every name checked against
; the grammar's node-types.json; see docs/adr/0103.

(comment) @comment
(string_value) @string
(integer_value) @number
(float_value) @number

(property_name) @keyword
(tag_name) @type
(class_name) @type
(id_name) @type
