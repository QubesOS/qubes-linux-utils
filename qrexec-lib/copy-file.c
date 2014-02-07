#include <unistd.h>
#include <ioall.h>
#include "libqubes-rpc-filecopy.h"
#include "crc32.h"

notify_progress_t *notify_progress_func = NULL;
void register_notify_progress(notify_progress_t *func)
{
	notify_progress_func = func;
}

int copy_file(int outfd, int infd, long long size, unsigned long *crc32)
{
	char buf[4096];
	long long written = 0;
	int ret;
	int count;
	while (written < size) {
		if (size - written > (int)sizeof(buf))
			count = sizeof buf;
		else
			count = size - written;
		ret = read(infd, buf, count);
		if (!ret)
			return COPY_FILE_READ_EOF;
		if (ret < 0)
			return COPY_FILE_READ_ERROR;
		/* acumulate crc32 if requested */
		if (crc32)
			*crc32 = Crc32_ComputeBuf(*crc32, buf, ret);
		if (!write_all(outfd, buf, ret))
			return COPY_FILE_WRITE_ERROR;
		if (notify_progress_func != NULL)
			notify_progress_func(ret, 0);
		written += ret;
	}
	return COPY_FILE_OK;
}

char * copy_file_status_to_str(int status)
{
	switch (status) {
		case COPY_FILE_OK: return "OK";
		case COPY_FILE_READ_EOF: return "Unexpected end of data while reading";
		case COPY_FILE_READ_ERROR: return "Error reading";
		case COPY_FILE_WRITE_ERROR: return "Error writing";
		default: return "????????";
	}
} 
