/*
 * Define _GNU_SOURCE so we get syscall(2).
 */
#define _GNU_SOURCE

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <linux/personality.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "configfile.h"
#include "copyfile.h"

#define set_pers(pers) ((long) syscall(SYS_personality, pers))

#ifndef SHELL_PATH
#	define SHELL_PATH	"/bin/sh"
#endif
#ifndef CONFIG_PATH
#	define CONFIG_PATH	"/etc/chpersroot.conf"
#endif
#ifndef COPY_IN
#	define COPY_IN
#endif


#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))


static const char *const ENV_TO_KEEP[] = {
	"TERM",
	"COLORTERM",
	"DISPLAY",
	"XAUTHORITY",
	NULL
};
static const char ENV_SUPATH[] = "/sbin:/bin:/usr/sbin:/usr/bin";
static const char ENV_PATH[] = "/bin:/usr/bin";

struct pw_env {
	const char name[8];
	const size_t offset;
};
static const struct pw_env ENV_FROM_PASSWD[] = {
	{ "HOME", offsetof(struct passwd, pw_dir) },
	{ "SHELL", offsetof(struct passwd, pw_shell) },
	{ "USER", offsetof(struct passwd, pw_name) },
	{ "LOGNAME", offsetof(struct passwd, pw_name) },
	{ "", -1 }
};

static const char *const COPY_IN_FILES[] = {
	COPY_IN
	NULL
};


static inline void*
xmalloc(size_t size)
{
	void* result = malloc(size);
	if (!result)
		err(EXIT_FAILURE, "out of memory");

	return result;
}

static void
switch_root(const char* root, const char* dir)
{
	if (chroot(root))
		err(EXIT_FAILURE, "chroot");
	if (chdir("/"))
		err(EXIT_FAILURE, "chdir to /");
	if (chdir(dir))
		err(EXIT_FAILURE, "chdir to home (%s)", dir);
}

static void
set_user(struct passwd* pw, gid_t* groups, int n_groups)
{
	if (setgroups(n_groups, groups))
		err(EXIT_FAILURE, "setgroups");
	if (setuid(pw->pw_uid))
		err(EXIT_FAILURE, "setuid to user");
	if (setgid(pw->pw_gid))
		err(EXIT_FAILURE, "setgid");
}

static inline int
need_sh_quote(char c)
{
	return (c == '\'' || c == '!');
}

static inline void
check_length(ptrdiff_t remaining, size_t needed)
{
	if (remaining < needed)
		errx(EXIT_FAILURE, "command line too long");
}

static char*
cmd_string(int argc, char* argv[])
{
	const size_t maxlen = (size_t) sysconf(_SC_ARG_MAX);
	char* cmd = xmalloc(maxlen);
	char* end = cmd + maxlen;
	char* p;

	for (p = cmd; argc > 0; --argc) {
		const char* src = *argv++;
		check_length(end - p, 2);
		if (p != cmd)
			*p++ = ' ';
		*p++ = '\'';
		while (*src) {
			size_t len = strcspn(src, "'!");
			check_length(end - p, len);
			strncpy(p, src, len);
			src += len;
			p += len;
			while (need_sh_quote(*src)) {
				check_length(end - p, 4);
				*p++ = '\'';
				*p++ = '\\';
				*p++ = *src++;
				*p++ = '\'';
			}
		}
		check_length(end - p, 2);
		*p++ = '\'';
	}
	*p = '\0';
	return cmd;
}

static inline const char*
xbasename(const char* path)
{
	const char* ret = strrchr(path, '/');
	if (!ret)
		ret = path;
	else
		++ret;
	return ret;
}

static char*
login_arg0(const char* arg0)
{
	const char* base = xbasename(arg0);
	const size_t len = strlen(base);
	char* ret = xmalloc(len + 2);
	ret[0] = '-';
	strncpy(ret + 1, base, len + 1);
	return ret;
}

static inline char*
make_env_var(const char* name, const char* value)
{
	size_t len = strlen(name) + strlen(value) + 2;
	char *ret = xmalloc(len);
	if (len != 1 + snprintf(ret, len, "%s=%s", name, value))
		errx(EXIT_FAILURE, "failed to copy environment");
	return ret;
}

static inline const char*
pw_at_offset(const struct passwd* pw, size_t offset)
{
	return *(const char**) ((const char*) pw + offset);
}

static char**
make_env(const struct passwd* pw)
{
	/*
	 * Add one to size of ENV_TO_KEEP and ENV_FROM_PASSWD to include
	 * $PATH (note that ENV_TO_KEEP includes a null terminator).
	 */
	char** envp = xmalloc(sizeof(char*) * (ARRAY_SIZE(ENV_TO_KEEP)
					+ ARRAY_SIZE(ENV_FROM_PASSWD) + 1));
	char** next_slot = envp;
	const char *const * to_keep;
	const struct pw_env* from_pw;

	*next_slot++ = make_env_var("PATH",
			pw->pw_uid ? ENV_PATH : ENV_SUPATH);

	for (from_pw = ENV_FROM_PASSWD; *from_pw->name; ++from_pw)
		*next_slot++ = make_env_var(from_pw->name,
				pw_at_offset(pw, from_pw->offset));

	for (to_keep = ENV_TO_KEEP; *to_keep; ++to_keep) {
		const char* value = getenv(*to_keep);
		if (!value)
			continue;
		*next_slot++ = make_env_var(*to_keep, value);
	}

	*next_slot = NULL;
	return envp;
}

static struct config_entry*
read_configuration(void)
{
	struct stat statbuf;
	struct config_entry* entry;
	int fd = open(CONFIG_PATH, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return NULL;
		err(EXIT_FAILURE, "read_configuration");
	}

	if (fstat(fd, &statbuf))
		err(EXIT_FAILURE, "stat");

	if (0 != statbuf.st_uid || 0 != statbuf.st_gid)
		errx(EXIT_FAILURE, "config file must be owned by root");

	if (S_IWGRP & statbuf.st_mode || S_IWOTH & statbuf.st_mode)
		errx(EXIT_FAILURE, "config file must not be world writable");

	if (parse_configfile(fd, &entry))
		errx(EXIT_FAILURE, "failed to parse config file");

	close(fd);
	return entry;
}

static void
copy_in_files(const char* rootdir, struct file_list* files)
{
	size_t rootlen = strlen(rootdir);
	struct file_list* entry;
	for (entry = files; entry; entry = entry->next) {
		size_t len = strlen(entry->file) + rootlen + 1;
		char *dstpath = xmalloc(len);
		snprintf(dstpath, len, "%s%s", rootdir, entry->file);
		if (copyfile(entry->file, dstpath))
			err(EXIT_FAILURE, "copyfile");
		free(dstpath);
	}
}

int
main(int argc, char* argv[])
{
	uid_t uid = getuid();
	struct passwd* pw;
	const char* target_config;
	char* cmd;
	char* args[4];
	gid_t* groups;
	int n_groups;
	char** envp;
	struct config_entry* config;

	pw = getpwuid(uid);
	if (!pw)
		err(EXIT_FAILURE, "getpwuid");
	n_groups = getgroups(0, NULL);
	groups = xmalloc(sizeof(gid_t) * n_groups);
	if (!getgroups(n_groups, groups))
		err(EXIT_FAILURE, "getgroups");

	cmd = pw->pw_shell;
	if (!cmd)
		cmd = SHELL_PATH;

	args[0] = login_arg0(cmd);
	if (argc > 1) {
		args[1] = "-c";
		args[2] = cmd_string(argc - 1, argv + 1);
		args[3] = NULL;
	} else {
		args[1] = NULL;
		/*
		 * Store the command (shell) in args[2] so that we can log
		 * this value regardless of which path is used to set up the
		 * argument array.
		 */
		args[2] = cmd;
	}

	target_config = xbasename(argv[0]);

	config = read_configuration();
	while (config) {
		if (!strcasecmp(target_config, config->name))
			break;
		config = config->next;
	}

	if (!config)
		errx(EXIT_FAILURE, "no such configuration: %s", target_config);
	if (!config->rootdir)
		errx(EXIT_FAILURE, "no root directory for configuration: %s",
			target_config);

	if (-1 != config->personality && set_pers(config->personality))
		err(EXIT_FAILURE, "set_pers");
	if (setuid(0))
		err(EXIT_FAILURE, "setuid to root");

	copy_in_files(config->rootdir, config->files_to_copy);

	/*
	 * Open the system log before we switch into the new root so that we
	 * are writing to the host's log.
	 */
	openlog(argv[0], LOG_NDELAY, LOG_AUTHPRIV);

	switch_root(config->rootdir, pw->pw_dir);
	set_user(pw, groups, n_groups);

	/* Setup restricted environment. */
	envp = make_env(pw);

	syslog(LOG_NOTICE,
		"[chpersroot user=\"%s\" command=\"%s\" root=\"%s\"]",
		pw->pw_name, args[2], config->rootdir);
	closelog();

	execve(cmd, args, envp);

	/*
	 * We only get here if exec fails.
	 */
	perror("failed to execute command");
	return 1;
}
