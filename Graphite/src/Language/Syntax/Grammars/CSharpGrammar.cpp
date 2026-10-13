#include "Language/Syntax/Grammars/CSharpGrammar.h"

#include "Language/Syntax/Grammar.h"

#include <tree_sitter/api.h>

#include <string_view>

extern "C" const TSLanguage* tree_sitter_c_sharp();

namespace Graphite {

	namespace {

		// Adapted from queries/highlights.scm of tree-sitter-c-sharp v0.23.5 (MIT; the notice is in
		// THIRD_PARTY_NOTICES.txt). Two captures of one node resolve to the later pattern, so the
		// catch-all `(identifier) @variable` has to stay first.
		constexpr std::string_view kHighlightQuery = R"scm(
(identifier) @variable

(method_declaration name: (identifier) @function)
(local_function_statement name: (identifier) @function)
(invocation_expression function: (identifier) @function)
(invocation_expression (member_access_expression name: (identifier) @function))

(interface_declaration name: (identifier) @type)
(class_declaration name: (identifier) @type)
(enum_declaration name: (identifier) @type)
(struct_declaration (identifier) @type)
(record_declaration (identifier) @type)

(explicit_interface_specifier (identifier) @type)
(explicit_interface_specifier (qualified_name name: (identifier) @type))

(namespace_declaration name: (identifier) @namespace)
(namespace_declaration name: (qualified_name name: (identifier) @namespace))
(file_scoped_namespace_declaration name: (identifier) @namespace)
(file_scoped_namespace_declaration name: (qualified_name name: (identifier) @namespace))
(using_directive (identifier) @namespace)
(using_directive (qualified_name name: (identifier) @namespace))
(qualified_name qualifier: (identifier) @namespace)
(qualified_name qualifier: (qualified_name name: (identifier) @namespace))

(generic_name (identifier) @type)

; After generic_name, because the later of two captures of one node wins: a generic name
; that is called is a method, and a method's return type sits in returns:, not type:.
(invocation_expression function: (generic_name (identifier) @function))
(invocation_expression function: (member_access_expression name: (generic_name (identifier) @function)))
(method_declaration returns: (identifier) @type)
(method_declaration returns: (qualified_name name: (identifier) @type))

(type_parameter (identifier) @type)
(parameter type: (identifier) @type)
(type_argument_list (identifier) @type)
(as_expression right: (identifier) @type)
(is_expression right: (identifier) @type)
(constructor_declaration name: (identifier) @type)
(destructor_declaration name: (identifier) @type)
(_ type: (identifier) @type)
(_ type: (qualified_name name: (identifier) @type))
(base_list (identifier) @type)
(type_parameter_constraints_clause (identifier) @type)
(attribute name: (identifier) @type)

(enum_member_declaration (identifier) @property)
(property_declaration name: (identifier) @property)

(parameter name: (identifier) @variable.parameter)

[
  (predefined_type)
  (implicit_type)
  (modifier)
  (boolean_literal)
  (null_literal)
  "this"
] @keyword

[
  (real_literal)
  (integer_literal)
] @number

(character_literal) @character

[
  (string_literal)
  (raw_string_literal)
  (verbatim_string_literal)
  (interpolated_string_expression)
  (interpolation_start)
  (interpolation_quote)
  (interpolation_brace)
] @string

(escape_sequence) @string.escape
(character_literal (escape_sequence) @character)

(comment) @comment

[
  "#if"
  "#elif"
  "#else"
  "#endif"
  "#region"
  "#endregion"
  "#define"
  "#undef"
  "#line"
  "#pragma"
  "#nullable"
  "#error"
  "#warning"
  (preproc_arg)
] @preprocessor

[
  ";"
  "."
  ","
] @punctuation.delimiter

[
  "--" "-" "-=" "&" "&=" "&&" "+" "++" "+=" "<" "<=" "<<" "<<=" "=" "==" "!" "!=" "=>"
  ">" ">=" ">>" ">>=" ">>>" ">>>=" "|" "|=" "||" "?" "??" "??=" "^" "^=" "~" "*" "*="
  "/" "/=" "%" "%=" ":" ".."
] @operator

[
  "(" ")" "[" "]" "{" "}"
] @punctuation.bracket

[
  "add" "alias" "as" "base" "break" "case" "catch" "checked" "class" "continue" "default"
  "delegate" "do" "else" "enum" "event" "explicit" "extern" "finally" "for" "foreach" "global"
  "goto" "if" "implicit" "interface" "is" "lock" "namespace" "notnull" "operator" "params"
  "return" "remove" "sizeof" "stackalloc" "static" "struct" "switch" "throw" "try" "typeof"
  "unchecked" "using" "while" "new" "await" "in" "yield" "get" "set" "when" "out" "ref" "from"
  "where" "select" "record" "init" "with" "let"
] @keyword
)scm";
	}

	const Grammar& CSharpGrammar()
	{
		// Built on first use, not during static initialisation.
		static const Grammar grammar(tree_sitter_c_sharp(), kHighlightQuery);

		return grammar;
	}
}
