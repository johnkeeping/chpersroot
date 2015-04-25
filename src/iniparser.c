#include "iniparser.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Define the buffer so that it pads out the iniparser_state structure to
 * 4096 bytes.
 */
#define BUFFER_SIZE	4096 - 2 * sizeof(void*) - 5 * sizeof(int)


struct iniparser_state {
	iniparser_callbacks* callbacks;
	void* cbdata;
	int fd;
	int lineno;
	int pos;
	int buflen;
	int state;

	char buf[BUFFER_SIZE];
};

typedef enum {
	STATE_START,
	STATE_START_CM, /* comment at start of file */
	STATE_CM, /* comment */
	STATE_LS, /* start of line */
	STATE_SH, /* section heading */
	STATE_LE, /* line end */
	STATE_EK, /* entry key */
	STATE_ES, /* entry separator */
	STATE_EV, /* entry value */
	STATE_SQ, /* single quoted value */
	STATE_DQ, /* double quoted value */
	STATE_EOF, /* end of file */
	STATE_ERROR
} parse_state;

iniparser*
iniparser_alloc(iniparser_callbacks* callbacks, void* cbdata)
{
	iniparser* parser = malloc(sizeof(iniparser));
	if (!parser)
		return NULL;

	parser->callbacks = callbacks;
	parser->cbdata = cbdata;
	parser->fd = -1;

	return parser;
}

void
iniparser_free(iniparser* parser)
{
	/*
	 * We shouldn't be free'ing a parser if it is currently parsing.
	 */
	assert(parser ? parser->fd < 0 : true);

	free(parser);
}

static inline int
parse_error(iniparser* parser, const char* msg)
{
	parser->state = STATE_ERROR;
	parser->callbacks->fatal_error(parser->cbdata, parser->lineno, msg);
	return -1;
}

static inline int
nextchar_simple(iniparser* parser)
{
	if (parser->pos >= parser->buflen) {
		parser->pos = 0;
		parser->buflen = read(parser->fd, parser->buf, BUFFER_SIZE);
		if (parser->buflen < 0) {
			return parse_error(parser, "IO error");
		} else if (parser->buflen == 0) {
			parser->state = STATE_EOF;
			return EOF;
		}
	}
	return parser->buf[parser->pos++];
}

static inline int
nextchar(iniparser* parser)
{
	int c, skip_lf;
	do {
		skip_lf = 0;
		c = nextchar_simple(parser);
		if (c == '\\') {
			/*
			 * Handle escaped EOL.
			 */
			int n = nextchar_simple(parser);
			if (n == '\r' || n == '\n') {
				skip_lf = 1;
				c = n;
			} else
				--parser->pos;
		}
		if (c == '\r') {
			/*
			 * Handle CRLF line breaks.  Note that decrementing
			 * pos works even if we refill the buffer in this call
			 * to nextchar_simple because it means pos is zero
			 * next time.
			 */
			int n = nextchar_simple(parser);
			if (n == '\n' || n < 0)
				c = n;
			else
				--parser->pos;
		}
		if (c == '\n')
			++parser->lineno;
	} while (skip_lf);
	return c;
}

typedef struct {
	size_t alloc;
	size_t len;
	char* str;
} buffer;
#define BUFFER_INIT { 0, 0, NULL }

static inline int
push_char(buffer* b, int c)
{
	if (b->len + 1 >= b->alloc) {
		size_t newsize = b->alloc ? b->alloc * 2 : 32;
		char* newptr = realloc(b->str, newsize);
		if (!newptr) {
			errno = ENOMEM;
			return -1;
		}
		b->alloc = newsize;
		b->str = newptr;
	}
	b->str[b->len++] = c;
	return 0;
}

static inline void
clear(buffer* b)
{
	b->len = 0;
}

static inline void
rstrip(buffer* b)
{
	while (b->len > 0) {
		if (isspace(b->str[b->len - 1]))
			--b->len;
		else
			break;
	}
}

static inline const char*
str(buffer* b)
{
	b->str[b->len] = '\0';
	return b->str;
}

static int
iniparser_parse(iniparser* parser)
{
	int c;
	buffer section = BUFFER_INIT, key = BUFFER_INIT, value = BUFFER_INIT;
	iniparser_callbacks* cb = parser->callbacks;
	void* cbdata = parser->cbdata;

	parser->state = STATE_START;
	while ((c = nextchar(parser)) >= 0) {
		switch (parser->state) {
		case STATE_START:
			if (isblank(c) || c == '\n')
				;
			else if (';' == c)
				parser->state = STATE_CM;
			else if ('[' == c)
				parser->state = STATE_SH;
			else
				return parse_error(parser,
					"expected section heading");
			break;
		case STATE_START_CM:
			if (c == '\n')
				parser->state = STATE_START;
			break;
		case STATE_CM:
			if (c == '\n')
				parser->state = STATE_LS;
			break;
		case STATE_LS:
			if (isblank(c) || c == '\n')
				;
			else if (c == ';')
				parser->state = STATE_CM;
			else if (c == '[') {
				clear(&section);
				parser->state = STATE_SH;
			} else {
				clear(&key);
				clear(&value);
				if (push_char(&key, c))
					return parse_error(parser, "out of memory");
				parser->state = STATE_EK;
			}
			break;
		case STATE_SH:
			if (c == ']') {
				c = cb->begin_section(cbdata, str(&section));
				if (c)
					return c;
				parser->state = STATE_LE;
			} else if (c == '\n')
				return parse_error(parser, "expected ']'");
			else if (push_char(&section, c))
				return parse_error(parser, "out of memory");
			break;
		case STATE_LE:
			if (isblank(c))
				;
			else if (c == ';')
				parser->state = STATE_CM;
			else if (c == '\n')
				parser->state = STATE_LS;
			else
				return parse_error(parser, "expected end of line");
			break;
		case STATE_EK:
			if (c == '=') {
				rstrip(&key);
				parser->state = STATE_ES;
			} else if (c == '\n')
				return parse_error(parser, "expected value with key");
			else if (push_char(&key, c))
				return parse_error(parser, "out of memory");
			break;
		case STATE_ES:
			if (isblank(c))
				;
			else if (c == '\'')
				parser->state = STATE_SQ;
			else if (c == '"')
				parser->state = STATE_DQ;
			else {
				if (push_char(&value, c))
					return parse_error(parser, "out of memory");
				parser->state = STATE_EV;
			}
			break;
		case STATE_EV:
			if (c == '\n' || c == ';') {
				parser->state = c == ';' ? STATE_CM : STATE_LS;
				rstrip(&value);
				c = cb->value_pair(cbdata, str(&key), str(&value));
				if (c)
					return c;
			} else if (push_char(&value, c))
				return parse_error(parser, "out of memory");
			break;
		case STATE_SQ:
			if (c == '\'') {
				c = cb->value_pair(cbdata, str(&key), str(&value));
				if (c)
					return c;
				parser->state = STATE_LS;
			} else if (c == '\n')
				return parse_error(parser, "expected single quote");
			else if (push_char(&value, c))
				return parse_error(parser, "out of memory");
			break;
		case STATE_DQ:
			if (c == '"') {
				c = cb->value_pair(cbdata, str(&key), str(&value));
				if (c)
					return c;
				parser->state = STATE_LS;
			} else if (c == '\n')
				return parse_error(parser, "expected single quote");
			else if (push_char(&value, c))
				return parse_error(parser, "out of memory");
			break;
		default:
			assert(false);
			break;
		}
	}

	switch (parser->state) {
	/*
	 * These conditions can just exit or have already reported an error.
	 */
	case STATE_START:
	case STATE_START_CM:
	case STATE_CM:
	case STATE_LS:
	case STATE_LE:
	case STATE_EOF:
	case STATE_ERROR:
		break;
	/*
	 * These are errors.
	 */
	case STATE_SH:
	case STATE_EK:
	case STATE_ES:
	case STATE_SQ:
	case STATE_DQ:
		return parse_error(parser, "unexpected end of file");
	/*
	 * We got to EOF while parsing a value, fire the callback.
	 */
	case STATE_EV:
		c = cb->value_pair(cbdata, str(&key), str(&value));
		if (c)
			return c;
		break;
	}
	return 0;
}

int
iniparser_parsefd(iniparser* parser, int fd)
{
	int result;
	assert(parser);
	assert(parser->fd < 0);

	parser->fd = fd;
	parser->pos = parser->buflen = 0;
	parser->lineno = 1;

	result = iniparser_parse(parser);
	parser->fd = -1;

	return result;
}
