/*
 * The Qubes OS Project, https://www.qubes-os.org
 *
 * Copyright (C) 2026  Piotr Bartman-Szwarc <prbartman@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

/*
 * Re-evaluate exported block devices when something is (un)mounted.
 *
 * Mounting a filesystem does not generate a block uevent.
 * Note: Swap is not covered.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGNAME "qubes-block-mount-watcher"
#define MOUNTINFO "/proc/self/mountinfo"
#define SYS_DEV_BLOCK "/sys/dev/block/"
#define MOUNTINFO_CHUNK 4096

/* "<major>:<minor>" as one integer, so the sets are sorted arrays of scalars:
 * easy diff without allocation. */
typedef uint64_t devid_t;

struct devset {
	devid_t *ids; /* sorted, unique */
	size_t count;
	size_t capacity;
};

static _Noreturn void die(const char *reason)
{
	fprintf(stderr, "%s: %s\n", PROGNAME, reason);
	exit(1);
}

static _Noreturn void die_errno(const char *reason)
{
	fprintf(stderr, "%s: %s: %s\n", PROGNAME, reason, strerror(errno));
	exit(1);
}

static void *xrealloc(void *ptr, size_t size)
{
	/* since C23 passing 0 as argument is undefined, glibc frees ptr and returns
	 * NULL, which we would interpret as an out-of-memory error. */
	if (size == 0)
		die("zero-sized allocation");

	void *result = realloc(ptr, size);
	if (result == NULL)
		die("out of memory");
	return result;
}

static int devid_cmp(const void *a, const void *b)
{
	devid_t left = *(const devid_t *)a;
	devid_t right = *(const devid_t *)b;

	if (left < right)
		return -1;
	return left > right;
}

static void devset_add(struct devset *set, devid_t id)
{
	/* Remember to sort after (see. `devset_sort_unique`) */
	if (set->count == set->capacity) {
		if (set->capacity > SIZE_MAX / (2 * sizeof(*set->ids)))
			die("too many mounts");
		set->capacity = set->capacity == 0 ? 64 : set->capacity * 2;
		set->ids = xrealloc(set->ids, set->capacity * sizeof(*set->ids));
	}
	set->ids[set->count++] = id;
}

static void devset_sort_unique(struct devset *set)
{
	size_t kept = 0;

	if (set->count == 0)
		return;
	qsort(set->ids, set->count, sizeof(*set->ids), devid_cmp);
	for (size_t i = 1; i < set->count; i++)
		if (set->ids[i] != set->ids[kept])
			set->ids[++kept] = set->ids[i];
	set->count = kept + 1;
}

/* One unsigned 32-bit number, terminated by *sep* or by end of line:
 *  - no sign
 *  - no leading space
 *  - nothing wider than the field can hold. */
static bool parse_u32(const char *text, char sep, unsigned long *out,
		      const char **rest)
{
	char *end;

	if (*text < '0' || *text > '9')
		return false;
	errno = 0;
	unsigned long value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || value > UINT32_MAX)
		return false;
	if (*end != sep && *end != '\0')
		return false;
	*out = value;
	/* move pointer */
	*rest = end;
	return true;
}

/* 3rd field is the number */
static bool parse_devid(const char *line, devid_t *out)
{
	const char *field = line;

	for (int i = 0; i < 2; i++) {
		field = strchr(field, ' ');
		if (field == NULL)
			return false;
		field++;
	}

	unsigned long major, minor;
	const char *rest;

	if (!parse_u32(field, ':', &major, &rest))
		return false;
	if (*rest != ':')
		return false;
	if (!parse_u32(rest + 1, ' ', &minor, &rest))
		return false;

	*out = ((devid_t)major << 32) | (devid_t)minor;
	return true;
}

/*
 * Device numbers of everything currently mounted.
 *
 * Re-reading also clears poll()'s pending-change state, so never skip it.
 */
static bool collect_mounted(int fd, struct devset *set)
{
	/* keep only a header of a line: "<id> <parent> <maj>:<min>";
	 * a longer header is dropped, not cut */
	char head[64];
	size_t head_len = 0;
	int spaces = 0;
	bool done_with_line = false;
	char buf[MOUNTINFO_CHUNK];

	set->count = 0;
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
		return false;

	for (;;) {
		ssize_t nread = read(fd, buf, sizeof(buf));
		if (nread < 0) {
			/* nothing was consumed */
			if (errno == EINTR)
				continue;
			return false;
		}
		if (nread == 0)
			break;
		for (ssize_t i = 0; i < nread; i++) {
			char c = buf[i];

			if (c == '\n') {
				/* reset, start new line*/
				head_len = 0;
				spaces = 0;
				done_with_line = false;
				continue;
			}
			if (done_with_line)
				/* cut everything after header */
				continue;
			if (c == ' ') {
				/* new part of header */
				spaces++;
				if (spaces == 3) {
					head[head_len] = '\0';
					devid_t id;
					if (parse_devid(head, &id))
						devset_add(set, id);
					/* we have all needed info from this line */
					done_with_line = true;
					continue;
				}
			}
			if (head_len + 1 >= sizeof(head)) {
				/* header is too long, ignore this line without parsing */
				done_with_line = true;
				continue;
			}
			head[head_len++] = c;
		}
	}

	/* a last line with no newline after it */
	if (!done_with_line && head_len > 0) {
		head[head_len] = '\0';
		devid_t id;
		if (parse_devid(head, &id))
			devset_add(set, id);
	}

	devset_sort_unique(set);
	return true;
}

/* Ask udev to re-run the rules for one sysfs directory.  Writing to its
 * "uevent" attribute is what `udevadm trigger` does, without the process. */
static void trigger(const char *dir)
{
	char path[PATH_MAX];

	int len = snprintf(path, sizeof(path), "%s/uevent", dir);
	if (len < 0 || (size_t)len >= sizeof(path))
		return;

	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	/* the device is gone in the meantime; nothing to do */
	static const char change[] = "change\n";
	ssize_t ignored = write(fd, change, sizeof(change) - 1);
	/* nothing to do if it fails; the next uevent recomputes anyway */
	(void)ignored;
	close(fd);
}

static void trigger_with_partitions(devid_t id)
{
	char dir[PATH_MAX];
	char link[PATH_MAX];
	char child[PATH_MAX];

	int len = snprintf(dir, sizeof(dir), SYS_DEV_BLOCK "%lu:%lu",
			   (unsigned long)(id >> 32),  // major
			   (unsigned long)(uint32_t)id);  //minor
	if (len < 0 || (size_t)len >= sizeof(dir))
		return;

	trigger(dir);

	ssize_t link_len = readlink(dir, link, sizeof(link) - 1);
	if (link_len <= 0)
		return; /* gone */
	if ((size_t)link_len == sizeof(link) - 1)
		return; /* truncated */
	link[link_len] = '\0';
	const char *name = strrchr(link, '/');  // last /
	name = name != NULL ? name + 1 : link;  // last part or whole link
	size_t name_len = strlen(name);
	if (name_len == 0)
		return;  // empty

	DIR *handle = opendir(dir);
	if (handle == NULL)
		return;
	struct dirent *entry;
	while ((entry = readdir(handle)) != NULL) {
		size_t entry_len = strlen(entry->d_name);
		if (entry_len <= name_len ||
		    strncmp(entry->d_name, name, name_len) != 0)
			continue;
		int child_len = snprintf(child, sizeof(child), "%s/%s", dir,
					 entry->d_name);
		if (child_len < 0 || (size_t)child_len >= sizeof(child))
			continue;
		trigger(child);
	}
	closedir(handle);
}

/* Devices that appear in *exactly one* of the two sorted sets. */
static void trigger_difference(const struct devset *previous,
			       const struct devset *current)
{
	size_t i = 0, j = 0;

	while (i < previous->count && j < current->count) {
		if (previous->ids[i] < current->ids[j])
			trigger_with_partitions(previous->ids[i++]);
		else if (previous->ids[i] > current->ids[j])
			trigger_with_partitions(current->ids[j++]);
		else
			i++, j++;
	}
	/* finish one or the other. */
	while (i < previous->count)
		trigger_with_partitions(previous->ids[i++]);
	while (j < current->count)
		trigger_with_partitions(current->ids[j++]);
}

int main(void)
{
	struct devset previous = { 0 }, current = { 0 };

	int fd = open(MOUNTINFO, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		die_errno(MOUNTINFO);

	if (!collect_mounted(fd, &previous))
		die_errno(MOUNTINFO);
	for (size_t i = 0; i < previous.count; i++)
		trigger_with_partitions(previous.ids[i]);

	struct pollfd pfd = { .fd = fd, .events = POLLPRI | POLLERR };
	for (;;) {
		if (poll(&pfd, 1, -1) < 0) {
			/* not a mount; wait again */
			if (errno == EINTR)
				continue;
			die_errno("poll");
		}
		if ((pfd.revents & POLLNVAL) != 0) {
			/* better to die and be restarted */
			die(MOUNTINFO " is no longer pollable");
		}
		if (!collect_mounted(fd, &current))
			die_errno(MOUNTINFO);
		trigger_difference(&previous, &current);

		struct devset swap = previous;
		previous = current;
		current = swap;
	}
}
