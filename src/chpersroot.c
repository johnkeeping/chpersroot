/*
 * Define _GNU_SOURCE so we get syscall().
 */
#define _GNU_SOURCE

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <linux/personality.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define set_pers(pers) ((long) syscall(SYS_personality, pers))

#ifndef ROOT_DIR
#	error "You must define ROOT_DIR to compile this file"
#endif


static inline void*
xmalloc(size_t size)
{
	void* result = malloc(size);
	if (!result)
		err(EXIT_FAILURE, "out of memory");

	return result;
}

static void
switch_to_linux32(void)
{
	if (set_pers(PER_LINUX32))
		err(EXIT_FAILURE, "set_pers");
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

int
main(int argc, char* argv[])
{
	uid_t uid = getuid();
	struct passwd* pw;
	char* cmd;
	char** args;
	gid_t* groups;
	int n_groups;

	pw = getpwuid(uid);
	if (!pw)
		err(EXIT_FAILURE, "getpwuid");
	n_groups = getgroups(0, NULL);
	groups = xmalloc(sizeof(gid_t) * n_groups);
	if (!getgroups(n_groups, groups))
		err(EXIT_FAILURE, "getgroups");

	if (argc > 1) {
		args = xmalloc(argc * sizeof(char*));
		memcpy(args, argv + 1, sizeof(char*) * (argc - 1));
		args[argc - 1] = NULL;
		/* TODO: Use "/bin/sh -c"? */
		cmd = args[0];
	} else {
		args = xmalloc(2 * sizeof(char*));
		/* FIXME: Use shell from pw. */
		cmd = "/bin/bash";
		args[0] = "-bash";
		args[1] = NULL;
	}

	switch_to_linux32();
	if (setuid(0))
		err(EXIT_FAILURE, "setuid to root");
	switch_root(ROOT_DIR, pw->pw_dir);
	if (setgroups(n_groups, groups))
		err(EXIT_FAILURE, "setgroups");
	if (setuid(uid))
		err(EXIT_FAILURE, "setuid to user");
	if (setgid(pw->pw_gid))
		err(EXIT_FAILURE, "setgid");

	/* FIXME: Setup restricted environment. */
	execvp(cmd, args);

	/*
	 * We only get here if exec fails.
	 */
	perror("failed to execute command");
	return 1;
}
