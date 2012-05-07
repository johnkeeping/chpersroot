#include "copyfile.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

const size_t BUFFER_SIZE = 1024;

static int
tmpdst(const char* dstpath, char** tmppath)
{
	size_t dstlen = strlen(dstpath);
	*tmppath = malloc(dstlen + 8);
	snprintf(*tmppath, dstlen + 8, "%s.XXXXXX", dstpath);

	return mkstemp(*tmppath);
}

int
copyfile(const char* srcpath, const char* dstpath)
{
	int srcfd, dstfd;
	struct stat statbuf;
	char* tmppath = NULL;
	char* buf = NULL;
	int retval = -1;

	srcfd = open(srcpath, O_RDONLY);
	if (srcfd < 0)
		goto err_src;

	dstfd = tmpdst(dstpath, &tmppath);
	if (dstfd < 0)
		goto err_dst;

	buf = malloc(BUFFER_SIZE);
	if (!buf) {
		errno = ENOMEM;
		goto err;
	}

	for (;;) {
		ssize_t w = 0, buf_start;
		ssize_t count = read(srcfd, buf, BUFFER_SIZE);
		if (count < 0)
			goto err;
		if (count == 0)
			break;

		for (buf_start = 0; buf_start < count; buf_start += w) {
			w = write(dstfd, buf + buf_start, count - buf_start);
			if (w < 0)
				goto err;
		}
	}

	if (fstat(srcfd, &statbuf))
		goto err;
	if (fchown(dstfd, statbuf.st_uid, statbuf.st_gid))
		goto err;
	if (fchmod(dstfd, statbuf.st_mode))
		goto err;

	if (close(dstfd))
		goto err;
	dstfd = -1;

	if (rename(tmppath, dstpath)) {
		unlink(tmppath);
		goto err;
	}

	retval = 0;

err:
	free(buf);
	if (dstfd >= 0) {
		close(dstfd);
		unlink(tmppath);
	}
err_dst:
	free(tmppath);
	close(srcfd);
err_src:
	return retval;
}
