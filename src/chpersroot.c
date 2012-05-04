/*
 * Define _GNU_SOURCE so we get syscall(2).
 */
#define _GNU_SOURCE

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <libgen.h>
#include <linux/personality.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define set_pers(pers) ((long) syscall(SYS_personality, pers))

#ifndef ROOT_DIR
#	error "You must define ROOT_DIR to compile this file"
#endif
#ifndef SHELL_PATH
#	define SHELL_PATH	"/bin/sh"
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
		err(EXIT_FAILURE, "chdir to home");
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

static char**
make_env(int system_path)
{
	/*
	 * Add one to size of ENV_TO_KEEP to include $PATH (note that
	 * ENV_TO_KEEP includes a null terminator.
	 */
	char** envp = xmalloc(sizeof(char*) * (ARRAY_SIZE(ENV_TO_KEEP) + 1));
	char** next_slot = envp;
	const char *const * to_keep;

	for (to_keep = ENV_TO_KEEP; *to_keep; ++to_keep) {
		const char* value = getenv(*to_keep);
		if (!value)
			continue;
		*next_slot++ = make_env_var(*to_keep, value);
	}

	*next_slot++ = make_env_var("PATH",
			system_path ? ENV_SUPATH : ENV_PATH);
	*next_slot = NULL;

	return envp;
}

int
main(int argc, char* argv[])
{
	uid_t uid = getuid();
	struct passwd* pw;
	char* cmd;
	char* args[4];
	gid_t* groups;
	int n_groups;
	char** envp;

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

	if (set_pers(PER_LINUX32))
		err(EXIT_FAILURE, "set_pers");
	if (setuid(0))
		err(EXIT_FAILURE, "setuid to root");

	/*
	 * Open the system log before we switch into the new root so that we
	 * are writing to the host's log.
	 */
	openlog(argv[0], LOG_NDELAY, LOG_AUTHPRIV);

	switch_root(ROOT_DIR, pw->pw_dir);
	set_user(pw, groups, n_groups);

	/* Setup restricted environment. */
	envp = make_env(pw->pw_uid == 0);

	syslog(LOG_NOTICE,
		"[chpersroot user=\"%s\" command=\"%s\" root=\"%s\"]",
		pw->pw_name, args[2], ROOT_DIR);
	closelog();

	execve(cmd, args, envp);

	/*
	 * We only get here if exec fails.
	 */
	perror("failed to execute command");
	return 1;
}
