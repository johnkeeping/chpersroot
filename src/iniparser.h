#ifndef INIPARSER_H
#define INIPARSER_H

typedef struct {
	int (*begin_section) (void* cbdata, const char* section_name);
	int (*value_pair) (void* cbdata, const char* key, const char* value);
	void (*fatal_error) (void* cbdata, int lineno, const char* msg);
} iniparser_callbacks;

typedef struct iniparser_state iniparser;

iniparser*
iniparser_alloc(iniparser_callbacks* callbacks, void* cbdata);

void
iniparser_free(iniparser* parser);

int
iniparser_parsefd(iniparser* parser, int fd);

#endif // INIPARSER_H
