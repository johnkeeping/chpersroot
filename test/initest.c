#include "iniparser.h"

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>


typedef struct {
	const char* section;
	const char* key;
	int active;
} print_config;

static int
test_begin_section(void* data, const char* section_name)
{
	print_config* config = data;
	config->active = !strcasecmp(section_name, config->section);
	return 0;
}

static int
test_value_pair(void* data, const char* key, const char* value)
{
	print_config* config = data;

	if (config->active && !strcasecmp(key, config->key))
		fprintf(stdout, "%s\n", value);

	return 0;
}

static void
test_fatal_error(void* data, int lineno, const char* msg)
{
	fprintf(stderr, "fatal error on line %d: %s\n", lineno, msg);
}

static iniparser_callbacks callbacks = {
	test_begin_section,
	test_value_pair,
	test_fatal_error
};

int
main(int argc, char* argv[])
{
	print_config config;
	int fd, retval = -1;
	iniparser* parser;
	if (4 != argc)
		errx(EXIT_FAILURE, "usage: %s [inifile] [section] [key]",
			argv[0]);

	config.section = argv[2];
	config.key = argv[3];
	config.active = 0;

	parser = iniparser_alloc(&callbacks, &config);
	if (!parser)
		errx(EXIT_FAILURE, "out of memory");

	fd = open(argv[1], O_RDONLY);
	if (fd < 1)
		err(EXIT_FAILURE, "open");

	retval = iniparser_parsefd(parser, fd);

	close(fd);
	return retval;
}
