#ifndef SYNTAX_H
#define SYNTAX_H

#include <stdbool.h>
#include "state.h"

/* Token types matching VSCode TextMate tokens */
typedef enum {
	TOK_NONE,           /* No special token */
	TOK_KEYWORD,        /* Control flow keywords (if, for, return, etc.) */
	TOK_STORAGE,        /* Storage types (int, char, struct, const, etc.) */
	TOK_STRING,         /* String literals */
	TOK_COMMENT,        /* Comments */
	TOK_NUMBER,         /* Numeric literals */
	TOK_FUNCTION,       /* Function names */
	TOK_TYPE,           /* Type names/classes */
	TOK_VARIABLE,       /* Variables/parameters */
	TOK_OPERATOR,       /* Operators */
	TOK_PREPROC,        /* Preprocessor directives (#include, etc.) */
	TOK_CONSTANT,       /* Constants (NULL, true, false, etc.) */
	TOK_TAG,            /* HTML/XML tags */
	TOK_ATTRIBUTE,      /* HTML/XML attributes */
	TOK_DECORATOR,      /* Python decorators, annotations */
	TOK_PROPERTY        /* Object properties/fields */
} TokenType;

/* Language types */
typedef enum {
	LANG_NONE,
	LANG_C,
	LANG_CPP,
	LANG_PYTHON,
	LANG_JAVASCRIPT,
	LANG_TYPESCRIPT,
	LANG_RUST,
	LANG_GO,
	LANG_JAVA,
	LANG_HTML,
	LANG_CSS,
	LANG_JSON,
	LANG_MARKDOWN,
	LANG_SHELL,
	LANG_YAML,
	LANG_TOML
} LangType;

/* Token information */
typedef struct {
	TokenType type;
	int start;          /* Start position in line */
	int end;            /* End position in line */
	const char *text;   /* Pointer into original text */
} Token;

/* Language detection from file extension */
LangType syntax_detect_language(const char *filename);
const char *syntax_language_name(LangType lang);

/* Tokenization for a line of code */
int syntax_tokenize_line(const char *line, int line_len, LangType lang,
                         Token *tokens, int max_tokens);

/* Get color for a token type */
Color syntax_token_color(TokenType type, LangType lang);

/* Check if a word is a keyword for the given language */
bool syntax_is_keyword(const char *word, LangType lang);

/* Check if a word is a storage type for the given language */
bool syntax_is_storage_type(const char *word, LangType lang);

/* Check if a word is a constant for the given language */
bool syntax_is_constant(const char *word, LangType lang);

/* Helper: check if character is word character */
bool syntax_is_word_char(char c);

/* Helper: check if character is operator */
bool syntax_is_operator_char(char c);

#endif
