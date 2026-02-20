#define _DEFAULT_SOURCE
#include "syntax.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "render.h" /* for TH */

/* ================================================================
   KEYWORD LISTS FOR DIFFERENT LANGUAGES
================================================================ */

/* C/C++ control flow keywords */
static const char *c_keywords[] = {
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "goto",
	"sizeof", "typeof",
	"alignof", "alignas",
	"static_assert",
	NULL
};

/* C/C++ storage types and modifiers */
static const char *c_storage[] = {
	"auto", "register", "static", "extern", "typedef", "thread_local",
	"const", "volatile", "restrict", "inline", "_Inline",
	"signed", "unsigned",
	"void", "char", "short", "int", "long", "float", "double", "bool",
	"struct", "union", "enum", "class",
	"typename", "template",
	"mutable", "virtual", "explicit", "friend", "constexpr", "noexcept",
	"nullptr", "decltype", "auto", "concept", "requires", "co_await",
	"co_return", "co_yield",
	"uint8_t", "uint16_t", "uint32_t", "uint64_t",
	"int8_t", "int16_t", "int32_t", "int64_t",
	"size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
	NULL
};

/* C/C++ constants */
static const char *c_constants[] = {
	"NULL", "true", "false", "null", "TRUE", "FALSE", "nullptr",
	"INT_MIN", "INT_MAX", "UINT_MAX", "SIZE_MAX",
	"EOF", "stdin", "stdout", "stderr",
	NULL
};

/* Python keywords */
static const char *py_keywords[] = {
	"if", "elif", "else", "for", "while", "break", "continue", "return",
	"yield", "raise", "pass", "del", "global", "nonlocal", "assert", "with",
	"as", "import", "from", "in", "is", "not", "and", "or", "lambda",
	"try", "except", "finally", "match", "case",
	"async", "await",
	NULL
};

/* Python storage types */
static const char *py_storage[] = {
	"def", "class", "type", "None", "True", "False",
	"int", "float", "str", "bytes", "bool", "list", "tuple", "dict", "set",
	"frozenset", "complex", "range", "enumerate", "reversed", "sorted",
	"object", "super", "self", "cls",
	NULL
};

/* Python constants */
static const char *py_constants[] = {
	"None", "True", "False", "Ellipsis", "NotImplemented", "__debug__",
	"Exception", "ValueError", "TypeError", "RuntimeError", "NameError",
	"ImportError", "KeyError", "AttributeError", "IndexError", "IOError",
	NULL
};

/* JavaScript/TypeScript keywords */
static const char *js_keywords[] = {
	"if", "else", "for", "while", "do", "switch", "case", "default", "break",
	"continue", "return", "throw", "try", "catch", "finally",
	"in", "instanceof", "typeof", "void", "delete",
	"await", "yield", "async", "of",
	"var", "let", "const", "function", "class", "extends", "super",
	"new", "this", "import", "export", "from", "as", "default",
	"get", "set", "static",
	NULL
};

/* JavaScript/TypeScript storage types */
static const char *js_storage[] = {
	"string", "number", "boolean", "void", "null", "undefined", "never", "unknown",
	"any", "object", "symbol", "bigint",
	"Array", "Object", "Function", "Promise", "Map", "Set", "WeakMap", "WeakSet",
	"interface", "type", "enum", "namespace", "module", "declare",
	"readonly", "abstract", "implements", "public", "private", "protected",
	"interface", "type", "enum", "namespace", "declare",
	"String", "Number", "Boolean", "Array", "Object",
	NULL
};

/* JavaScript constants */
static const char *js_constants[] = {
	"null", "undefined", "NaN", "Infinity", "true", "false",
	"console", "window", "document", "global", "globalThis", "process",
	NULL
};

/* Rust keywords */
static const char *rust_keywords[] = {
	"if", "else", "match", "break", "continue", "return", "loop", "while", "for",
	"in", "move", "where", "async", "await",
	NULL
};

/* Rust storage types */
static const char *rust_storage[] = {
	"fn", "let", "const", "static", "mut", "ref",
	"struct", "enum", "union", "trait", "impl", "type", "use", "mod", "crate",
	"u8", "u16", "u32", "u64", "u128", "usize",
	"i8", "i16", "i32", "i64", "i128", "isize",
	"f32", "f64", "bool", "char", "str", "String", "Vec", "Box", "Option",
	"Result", "Some", "None", "Ok", "Err",
	"self", "Self", "super",
	"pub", "crate", "extern", "unsafe",
	NULL
};

/* Rust constants */
static const char *rust_constants[] = {
	"true", "false", "None", "Some", "Ok", "Err", "self", "Self",
	NULL
};

/* Go keywords */
static const char *go_keywords[] = {
	"if", "else", "for", "range", "break", "continue", "return", "goto",
	"switch", "case", "default", "fallthrough", "select", "defer", "go",
	"chan", "map", "struct", "interface",
	"package", "import", "var", "const",
	"goto", "type", "func",
	NULL
};

/* Go storage types */
static const char *go_storage[] = {
	"int", "int8", "int16", "int32", "int64",
	"uint", "uint8", "uint16", "uint32", "uint64",
	"float32", "float64", "complex64", "complex128",
	"bool", "string", "byte", "rune",
	"error", "any", "comparable",
	"struct", "interface", "map", "chan", "func",
	"nil", "make", "new",
	NULL
};

/* Go constants */
static const char *go_constants[] = {
	"true", "false", "nil", "iota",
	NULL
};

/* Java keywords */
static const char *java_keywords[] = {
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "goto", "throw", "throws", "try", "catch",
	"finally", "assert", "instanceof",
	"new", "this", "super",
	"package", "import",
	"class", "interface", "extends", "implements", "abstract",
	NULL
};

/* Java storage types */
static const char *java_storage[] = {
	"boolean", "char", "byte", "short", "int", "long", "float", "double", "void",
	"var", "val", "const", "static", "final", "synchronized", "volatile", "transient",
	"strictfp", "native", "private", "public", "protected", "static",
	"String", "Object", "Integer", "Long", "Double", "Float", "Boolean",
	NULL
};

/* Java constants */
static const char *java_constants[] = {
	"true", "false", "null", "this", "super",
	NULL
};

/* Shell keywords */
static const char *sh_keywords[] = {
	"if", "then", "elif", "else", "fi", "for", "while", "do", "done", "case",
	"esac", "select", "function", "time", "until", "in", "return",
	NULL
};

/* Shell storage types (builtins) */
static const char *sh_storage[] = {
	"local", "declare", "typeset", "export", "readonly", "unset", "shift",
	"echo", "printf", "read", "source", ".", "command", "builtin",
	"true", "false", "test", "[", "]]",
	NULL
};

/* HTML/Tag keywords */
static const char *html_tags[] = {
	"div", "span", "p", "a", "img", "br", "hr", "h1", "h2", "h3", "h4", "h5",
	"h6", "ul", "ol", "li", "table", "tr", "td", "th", "thead", "tbody", "tfoot",
	"form", "input", "button", "select", "option", "textarea", "label", "fieldset",
	"legend", "section", "article", "aside", "nav", "header", "footer", "main",
	"body", "head", "html", "title", "meta", "link", "script", "style", "noscript",
	"iframe", "canvas", "svg", "path", "rect", "circle", "ellipse", "line", "polygon",
	"strong", "em", "b", "i", "u", "code", "pre", "blockquote", "q", "cite",
	NULL
};

/* ================================================================
   LANGUAGE DETECTION
================================================================ */

LangType syntax_detect_language(const char *filename) {
	if (!filename) return LANG_NONE;

	const char *ext = strrchr(filename, '.');
	if (!ext) {
		/* Check for special files */
		if (strcmp(filename, "Makefile") == 0 ||
		    strcmp(filename, "makefile") == 0 ||
		    strcmp(filename, "Dockerfile") == 0 ||
		    strcmp(filename, ".gitignore") == 0 ||
		    strcmp(filename, ".gitattributes") == 0)
			return LANG_SHELL;
		if (strcmp(filename, "Cargo.toml") == 0)
			return LANG_TOML;
		if (strcmp(filename, "go.mod") == 0)
			return LANG_GO;
		return LANG_NONE;
	}
	ext++;

	if (strcmp(ext, "c") == 0) return LANG_C;
	if (strcmp(ext, "h") == 0) return LANG_C;
	if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 ||
	    strcmp(ext, "cxx") == 0 || strcmp(ext, "hpp") == 0 ||
	    strcmp(ext, "hxx") == 0) return LANG_CPP;
	if (strcmp(ext, "py") == 0 || strcmp(ext, "pyi") == 0) return LANG_PYTHON;
	if (strcmp(ext, "js") == 0 || strcmp(ext, "mjs") == 0 ||
	    strcmp(ext, "cjs") == 0) return LANG_JAVASCRIPT;
	if (strcmp(ext, "ts") == 0 || strcmp(ext, "tsx") == 0) return LANG_TYPESCRIPT;
	if (strcmp(ext, "rs") == 0) return LANG_RUST;
	if (strcmp(ext, "go") == 0) return LANG_GO;
	if (strcmp(ext, "java") == 0) return LANG_JAVA;
	if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0 ||
	    strcmp(ext, "xhtml") == 0) return LANG_HTML;
	if (strcmp(ext, "css") == 0 || strcmp(ext, "scss") == 0 ||
	    strcmp(ext, "sass") == 0 || strcmp(ext, "less") == 0) return LANG_CSS;
	if (strcmp(ext, "json") == 0) return LANG_JSON;
	if (strcmp(ext, "md") == 0 || strcmp(ext, "markdown") == 0) return LANG_MARKDOWN;
	if (strcmp(ext, "sh") == 0 || strcmp(ext, "bash") == 0 ||
	    strcmp(ext, "zsh") == 0) return LANG_SHELL;
	if (strcmp(ext, "yml") == 0 || strcmp(ext, "yaml") == 0) return LANG_YAML;
	if (strcmp(ext, "toml") == 0) return LANG_TOML;

	return LANG_NONE;
}

const char *syntax_language_name(LangType lang) {
	switch (lang) {
		case LANG_C: return "C";
		case LANG_CPP: return "C++";
		case LANG_PYTHON: return "Python";
		case LANG_JAVASCRIPT: return "JavaScript";
		case LANG_TYPESCRIPT: return "TypeScript";
		case LANG_RUST: return "Rust";
		case LANG_GO: return "Go";
		case LANG_JAVA: return "Java";
		case LANG_HTML: return "HTML";
		case LANG_CSS: return "CSS";
		case LANG_JSON: return "JSON";
		case LANG_MARKDOWN: return "Markdown";
		case LANG_SHELL: return "Shell";
		case LANG_YAML: return "YAML";
		case LANG_TOML: return "TOML";
		default: return "Plain Text";
	}
}

/* ================================================================
   KEYWORD/TYPE CHECKING
================================================================ */

static bool string_in_list(const char *str, const char **list) {
	if (!str || !list) return false;
	for (int i = 0; list[i]; i++) {
		if (strcmp(str, list[i]) == 0) return true;
	}
	return false;
}

bool syntax_is_keyword(const char *word, LangType lang) {
	if (!word) return false;

	switch (lang) {
		case LANG_C:
		case LANG_CPP:
			return string_in_list(word, c_keywords);
		case LANG_PYTHON:
			return string_in_list(word, py_keywords);
		case LANG_JAVASCRIPT:
		case LANG_TYPESCRIPT:
			return string_in_list(word, js_keywords);
		case LANG_RUST:
			return string_in_list(word, rust_keywords);
		case LANG_GO:
			return string_in_list(word, go_keywords);
		case LANG_JAVA:
			return string_in_list(word, java_keywords);
		case LANG_SHELL:
			return string_in_list(word, sh_keywords);
		default:
			return false;
	}
}

bool syntax_is_storage_type(const char *word, LangType lang) {
	if (!word) return false;

	switch (lang) {
		case LANG_C:
		case LANG_CPP:
			return string_in_list(word, c_storage);
		case LANG_PYTHON:
			return string_in_list(word, py_storage);
		case LANG_JAVASCRIPT:
		case LANG_TYPESCRIPT:
			return string_in_list(word, js_storage);
		case LANG_RUST:
			return string_in_list(word, rust_storage);
		case LANG_GO:
			return string_in_list(word, go_storage);
		case LANG_JAVA:
			return string_in_list(word, java_storage);
		case LANG_SHELL:
			return string_in_list(word, sh_storage);
		case LANG_HTML:
			return string_in_list(word, html_tags);
		default:
			return false;
	}
}

bool syntax_is_constant(const char *word, LangType lang) {
	if (!word) return false;

	switch (lang) {
		case LANG_C:
		case LANG_CPP:
			return string_in_list(word, c_constants);
		case LANG_PYTHON:
			return string_in_list(word, py_constants);
		case LANG_JAVASCRIPT:
		case LANG_TYPESCRIPT:
			return string_in_list(word, js_constants);
		case LANG_RUST:
			return string_in_list(word, rust_constants);
		case LANG_GO:
			return string_in_list(word, go_constants);
		case LANG_JAVA:
			return string_in_list(word, java_constants);
		default:
			return false;
	}
}

/* ================================================================
   CHARACTER CLASSIFICATION
================================================================ */

bool syntax_is_word_char(char c) {
	return isalnum((unsigned char)c) || c == '_';
}

bool syntax_is_operator_char(char c) {
	return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
	       c == '=' || c == '!' || c == '<' || c == '>' || c == '&' ||
	       c == '|' || c == '^' || c == '~' || c == '?' || c == ':' ||
	       c == '.';
}

/* ================================================================
   TOKEN COLOR MAPPING
================================================================ */

Color syntax_token_color(TokenType type, LangType lang) {
	switch (type) {
		case TOK_KEYWORD:
			return TH->syn_keyword;
		case TOK_STORAGE:
			return TH->syn_storage;
		case TOK_STRING:
			return TH->syn_string;
		case TOK_COMMENT:
			return TH->syn_comment;
		case TOK_NUMBER:
			return TH->syn_number;
		case TOK_FUNCTION:
			return TH->syn_function;
		case TOK_TYPE:
			return TH->syn_type;
		case TOK_VARIABLE:
			return TH->syn_variable;
		case TOK_OPERATOR:
			return TH->syn_operator;
		case TOK_PREPROC:
			return TH->syn_preproc;
		case TOK_CONSTANT:
			return TH->syn_constant;
		case TOK_TAG:
			return TH->syn_tag;
		case TOK_ATTRIBUTE:
			return TH->syn_attribute;
		case TOK_DECORATOR:
			return TH->syn_decorator;
		case TOK_PROPERTY:
			return TH->syn_variable;
		default:
			return TH->fg_normal;
	}
}

/* ================================================================
   LINE TOKENIZATION
================================================================ */

int syntax_tokenize_line(const char *line, int line_len, LangType lang,
                         Token *tokens, int max_tokens) {
	if (!line || line_len <= 0 || !tokens || max_tokens <= 0)
		return 0;

	int token_count = 0;
	int i = 0;
	bool in_string = false;
	bool in_char = false;
	bool in_comment = false;
	bool in_line_comment = false;
	bool in_template = false;
	char string_char = 0;

	while (i < line_len && token_count < max_tokens - 1) {
		/* Skip whitespace */
		while (i < line_len && isspace((unsigned char)line[i])) i++;
		if (i >= line_len) break;

		int start = i;

		/* Check for line comments */
		if (!in_comment && !in_string && !in_char) {
			if ((lang == LANG_C || lang == LANG_CPP || lang == LANG_JAVA ||
			     lang == LANG_JAVASCRIPT || lang == LANG_TYPESCRIPT ||
			     lang == LANG_GO || lang == LANG_RUST) &&
			    line[i] == '/' && i + 1 < line_len && line[i + 1] == '/') {
				tokens[token_count].type = TOK_COMMENT;
				tokens[token_count].start = i;
				tokens[token_count].end = line_len;
				tokens[token_count].text = line + i;
				token_count++;
				break;
			}
			if (lang == LANG_PYTHON && line[i] == '#') {
				tokens[token_count].type = TOK_COMMENT;
				tokens[token_count].start = i;
				tokens[token_count].end = line_len;
				tokens[token_count].text = line + i;
				token_count++;
				break;
			}
			if (lang == LANG_SHELL && line[i] == '#') {
				tokens[token_count].type = TOK_COMMENT;
				tokens[token_count].start = i;
				tokens[token_count].end = line_len;
				tokens[token_count].text = line + i;
				token_count++;
				break;
			}
		}

		/* Check for preprocessor directives */
		if (!in_string && !in_char && !in_comment && i == 0 && line[i] == '#') {
			while (i < line_len && syntax_is_word_char(line[i])) i++;
			tokens[token_count].type = TOK_PREPROC;
			tokens[token_count].start = start;
			tokens[token_count].end = i;
			tokens[token_count].text = line + start;
			token_count++;
			continue;
		}

		/* Check for string/char literals */
		if (!in_comment) {
			if (!in_string && !in_char &&
			    (line[i] == '"' || line[i] == '\'' ||
			     (lang == LANG_PYTHON && line[i] == 'r' && i + 1 < line_len &&
			      (line[i + 1] == '"' || line[i + 1] == '\'')) ||
			     (lang == LANG_PYTHON && line[i] == 'f' && i + 1 < line_len &&
			      (line[i + 1] == '"' || line[i + 1] == '\'')) ||
			     (lang == LANG_JAVASCRIPT && line[i] == '`'))) {

				if (lang == LANG_PYTHON && (line[i] == 'r' || line[i] == 'f')) {
					i++; /* skip r/f prefix */
				}
				if (lang == LANG_JAVASCRIPT && line[i] == '`') {
					in_template = true;
				}

				in_string = true;
				string_char = line[i];
				i++;
				while (i < line_len) {
					if (line[i] == '\\' && i + 1 < line_len) {
						i += 2;
						continue;
					}
					if (line[i] == string_char) {
						i++;
						break;
					}
					if (line[i] == '\n') break;
					i++;
				}
				in_string = false;
				in_template = false;

				tokens[token_count].type = TOK_STRING;
				tokens[token_count].start = start;
				tokens[token_count].end = i;
				tokens[token_count].text = line + start;
				token_count++;
				continue;
			}
		}

		/* Check for numbers */
		if (!in_string && !in_char && !in_comment &&
		    (isdigit((unsigned char)line[i]) ||
		     (line[i] == '0' && i + 1 < line_len &&
		      (line[i + 1] == 'x' || line[i + 1] == 'X' || line[i + 1] == 'b' ||
		       line[i + 1] == 'B' || line[i + 1] == 'o' || line[i + 1] == 'O')) ||
		     (line[i] == '.' && i + 1 < line_len &&
		      isdigit((unsigned char)line[i + 1])))) {

			if (line[i] == '0' && i + 1 < line_len) {
				char next = line[i + 1];
				if (next == 'x' || next == 'X') {
					i += 2;
					while (i < line_len && isxdigit((unsigned char)line[i])) i++;
				} else if (next == 'b' || next == 'B') {
					i += 2;
					while (i < line_len && (line[i] == '0' || line[i] == '1')) i++;
				} else if (next == 'o' || next == 'O') {
					i += 2;
					while (i < line_len && line[i] >= '0' && line[i] <= '7') i++;
				} else {
					while (i < line_len && isdigit((unsigned char)line[i])) i++;
				}
			} else {
				while (i < line_len && isdigit((unsigned char)line[i])) i++;
				if (i < line_len && line[i] == '.') {
					i++;
					while (i < line_len && isdigit((unsigned char)line[i])) i++;
				}
				if (i < line_len && (line[i] == 'e' || line[i] == 'E')) {
					i++;
					if (i < line_len && (line[i] == '+' || line[i] == '-')) i++;
					while (i < line_len && isdigit((unsigned char)line[i])) i++;
				}
			}

			/* Handle suffixes */
			if (i < line_len) {
				if (lang == LANG_C || lang == LANG_CPP ||
				    lang == LANG_JAVASCRIPT || lang == LANG_TYPESCRIPT) {
					while (i < line_len &&
					       (line[i] == 'u' || line[i] == 'U' || line[i] == 'l' ||
					        line[i] == 'L' || line[i] == 'f' || line[i] == 'F'))
						i++;
				}
				if (lang == LANG_RUST) {
					while (i < line_len &&
					       (line[i] == 'u' || line[i] == 'i' || line[i] == 'f' ||
					        line[i] == '8' || line[i] == '16' || line[i] == '32' ||
					        line[i] == '64' || line[i] == 's' || line[i] == 'z'))
						i++;
				}
				if (lang == LANG_GO) {
					while (i < line_len && line[i] >= '0' && line[i] <= '9') i++;
				}
			}

			tokens[token_count].type = TOK_NUMBER;
			tokens[token_count].start = start;
			tokens[token_count].end = i;
			tokens[token_count].text = line + start;
			token_count++;
			continue;
		}

		/* Check for decorators (Python, TypeScript) */
		if (!in_string && !in_char && !in_comment && line[i] == '@') {
			while (i < line_len && syntax_is_word_char(line[i])) i++;
			while (i < line_len && line[i] == '.') i++;
			while (i < line_len && syntax_is_word_char(line[i])) i++;

			tokens[token_count].type = TOK_DECORATOR;
			tokens[token_count].start = start;
			tokens[token_count].end = i;
			tokens[token_count].text = line + start;
			token_count++;
			continue;
		}

		/* Check for operators */
		if (!in_string && !in_char && !in_comment && syntax_is_operator_char(line[i])) {
			/* Multi-char operators */
			if (i + 1 < line_len) {
				char two[3] = {line[i], line[i + 1], 0};
				if (strcmp(two, "==") == 0 || strcmp(two, "!=") == 0 ||
				    strcmp(two, "<=") == 0 || strcmp(two, ">=") == 0 ||
				    strcmp(two, "&&") == 0 || strcmp(two, "||") == 0 ||
				    strcmp(two, "++") == 0 || strcmp(two, "--") == 0 ||
				    strcmp(two, "<<") == 0 || strcmp(two, ">>") == 0 ||
				    strcmp(two, "+=") == 0 || strcmp(two, "-=") == 0 ||
				    strcmp(two, "*=") == 0 || strcmp(two, "/=") == 0 ||
				    strcmp(two, "%=") == 0 || strcmp(two, "&=") == 0 ||
				    strcmp(two, "|=") == 0 || strcmp(two, "^=") == 0 ||
				    strcmp(two, "->") == 0 || strcmp(two, "=>") == 0 ||
				    strcmp(two, "::") == 0 || strcmp(two, ".*") == 0) {
					i += 2;
					tokens[token_count].type = TOK_OPERATOR;
					tokens[token_count].start = start;
					tokens[token_count].end = i;
					tokens[token_count].text = line + start;
					token_count++;
					continue;
				}
			}
			/* Single char operator */
			i++;
			tokens[token_count].type = TOK_OPERATOR;
			tokens[token_count].start = start;
			tokens[token_count].end = i;
			tokens[token_count].text = line + start;
			token_count++;
			continue;
		}

		/* Extract word */
		if (syntax_is_word_char(line[i])) {
			while (i < line_len && syntax_is_word_char(line[i])) i++;

			/* Copy word to check */
			char word[128];
			int word_len = i - start;
			if (word_len >= (int)sizeof(word)) word_len = sizeof(word) - 1;
			memcpy(word, line + start, word_len);
			word[word_len] = '\0';

			TokenType type = TOK_NONE;

			/* Check for constants first */
			if (syntax_is_constant(word, lang)) {
				type = TOK_CONSTANT;
			}
			/* Check for keywords */
			else if (syntax_is_keyword(word, lang)) {
				type = TOK_KEYWORD;
			}
			/* Check for storage types */
			else if (syntax_is_storage_type(word, lang)) {
				type = TOK_STORAGE;
			}
			/* Check for HTML tags (after < or after </) */
			else if (lang == LANG_HTML && start > 0 &&
			         (line[start - 1] == '<' ||
			          (start >= 2 && line[start - 2] == '<' && line[start - 1] == '/'))) {
				type = TOK_TAG;
			}
			/* Heuristic for function calls */
			else if (i < line_len && line[i] == '(') {
				type = TOK_FUNCTION;
			}
			/* Default to variable/identifier */
			else {
				type = TOK_VARIABLE;
			}

			tokens[token_count].type = type;
			tokens[token_count].start = start;
			tokens[token_count].end = i;
			tokens[token_count].text = line + start;
			token_count++;
			continue;
		}

		/* Any other character - skip */
		i++;
	}

	return token_count;
}
