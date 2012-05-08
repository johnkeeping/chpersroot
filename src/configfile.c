#include "configfile.h"
#include "iniparser.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#define BUFFER_SIZE	4096 - 2 * sizeof(struct config_entry*) \
				- 3 * sizeof(int) - sizeof(FILE*)

struct parse_state {
	struct config_entry* first_entry;
	struct config_entry* current_entry;
};

void
free_file_list(struct file_list* list) {
	while (list) {
		struct file_list* next = list->next;
		free(list->file);
		list = next;
		free(list);
	}
}

void
free_config_entry(struct config_entry* entry) {
	if (!entry)
		return;

	free(entry->name);
	free(entry->rootdir);
	free_file_list(entry->files_to_copy);
	free(entry);
}

void
free_config_entries(struct config_entry* entries) {
	while (entries) {
		struct config_entry* next = entries->next;
		free_config_entry(entries);
		entries = next;
	}
}

static int
config_begin_section(void* data, const char* section_name)
{
	struct parse_state* state = data;
	struct config_entry* entry = calloc(1, sizeof(struct config_entry));
	if (!entry) {
		errno = ENOMEM;
		return -1;
	}

	entry->name = strdup(section_name);
	if (!entry->name)
		return -1;

	if (state->current_entry)
		state->current_entry->next = entry;
	else
		state->first_entry = entry;

	state->current_entry = entry;
	return 0;
}

static int
config_value_pair(void* data, const char* key, const char* value)
{
	struct parse_state* state = data;
	struct config_entry* entry = state->current_entry;

	if (!entry)
		return 0;

	if (!strcasecmp(key, "rootdir")) {
		entry->rootdir = strdup(value);
		if (!entry->rootdir)
			return -1;
	} else if (!strcasecmp(key, "copyfile")) {
		struct file_list* fl = calloc(1, sizeof(struct file_list));
		if (!fl) {
			errno = ENOMEM;
			return -1;
		}
		fl->file = strdup(value);
		if (!fl->file)
			return -1;
		fl->next = entry->files_to_copy;
		entry->files_to_copy = fl;
	} else
		fprintf(stderr, "warning: unknown configuration key: %s\n", key);

	return 0;
}

static void
config_fatal_error(void* data, int lineno, const char* msg)
{
	fprintf(stderr, "configuration file error on line %d: %s\n",
		lineno, msg);
}

static iniparser_callbacks config_callbacks = {
	config_begin_section,
	config_value_pair,
	config_fatal_error
};

static inline void
reverse_files_to_copy(struct config_entry* entry) {
	while (entry) {
		struct file_list* next = NULL;
		struct file_list* cur = entry->files_to_copy;
		while (cur) {
			struct file_list* t = cur->next;
			cur->next = next;
			next = cur;
			cur = t;
		}
		entry = entry->next;
	}
}

int
parse_configfile(int fd, struct config_entry** entries)
{
	int retval = -1;
	struct parse_state* state = calloc(1, sizeof(struct parse_state));
	iniparser* parser = iniparser_alloc(&config_callbacks, state);
	if (!state || !parser) {
		errno = ENOMEM;
		goto err;
	}

	if (iniparser_parsefd(parser, fd))
		goto err;

	*entries = state->first_entry;
	state->first_entry = NULL;
	retval = 0;

err:
	iniparser_free(parser);
	if (state)
		free_config_entries(state->first_entry);
	free(state);
	return retval;
}
