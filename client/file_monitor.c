#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/inotify.h>

#include <debug.h>
#include <consts.h>
#include <file_table.h>
#include <trans_file_table.h>
#include "file_monitor.h"

static int parse_conf_file(char **target_dirs, int max_n)
{
	FILE *fp;
	char dir_name[MAX_LINE];
	int n = 0;

	fp = fopen(CONF_FILE, "r");
	if (fp == NULL) {
		_error("open '%s' failed\n", CONF_FILE);
		return -1;
	}

	while (fscanf(fp, "%s", dir_name) != EOF && n < max_n) {
		if (dir_name[strlen(dir_name) - 1] == '/')
			dir_name[strlen(dir_name) - 1] = '\0';

		target_dirs[n] = calloc(1, strlen(dir_name) + 1);
		if (target_dirs[n] == NULL) {
			_error("calloc failed\n");
			fclose(fp);
			return -1;
		}
		strcpy(target_dirs[n], dir_name);

		n++;
	}

	fclose(fp);
	return n;
}

static inline void free_target_dirs(char **target_dirs, int n)
{
	int i;
	for (i = 0; i < n; i++)
		free(target_dirs[i]);
}

static inline void unwatch_targets(int fd, int *wds, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (wds[i] <= 0)
			break;
		inotify_rm_watch(fd, wds[i]);
	}
	free(wds);
}

static void get_operation_type(struct trans_file_entry *file_e,
			       struct inotify_event *event,
			       char *target_dir)
{
	if (event->mask & IN_CREATE) {
		file_e->op_type = FILE_ADD;
		_debug("'%s' created\n", file_e->name);
	} else if (event->mask & IN_DELETE) {
		file_e->op_type = FILE_DELETE;
		_debug("'%s' deleted\n", file_e->name);
	} else if (event->mask & IN_DELETE_SELF) {
		file_e->op_type = FILE_DELETE;
		_debug("Target '%s' was itself deleted\n", target_dir);
	} else if (event->mask & IN_MODIFY) {
		file_e->op_type = FILE_MODIFY;
		_debug("'%s' was modified\n", file_e->name);
	} else if (event->mask & IN_MOVE_SELF) {
		file_e->op_type = FILE_DELETE;
		_debug("Target '%s' was itself moved", target_dir);
	} else if (event->mask & IN_MOVED_FROM) {
		file_e->op_type = FILE_DELETE;
		_debug("'%s' moved out\n", file_e->name);
	} else if (event->mask & IN_MOVED_TO) {
		file_e->op_type = FILE_ADD;
		_debug("'%s' moved into\n", file_e->name);
	}
}

inline static int get_timestamp(struct trans_file_entry *file_e)
{
	struct stat st;
	if (stat(file_e->name, &st) < 0) {
		perror("stat error");
		return -1;
	} else {
		file_e->timestamp = st.st_mtime;
		return 0;
	}
}

int file_monitor_task()
{
	char *target_dirs[MAX_TARGET_DIR] = { 0 };
	char *wd_target_name[WD_HASH_SIZE] = { 0 };
	char event_buf[EVENT_BUF_LEN] = { 0 };
	struct inotify_event *event;
	struct trans_file_table file_table;
	int i, offset, target_n, fd, *wds;
	int ret = -1;
	ssize_t len;

	target_n = parse_conf_file(target_dirs, MAX_LINE);
	if (target_n <= 0)
		return -1;

	fd = inotify_init();
	if (fd < 0) {
		_error("inotify_init failed\n");
		goto free_target_dirs;
	}

	wds = calloc(target_n, sizeof(int));
	if (wds == NULL) {
		_error("calloc failed\n");
		goto close_fd;
	}

	for (i = 0; i < target_n; i++) {
		wds[i] = inotify_add_watch(fd, target_dirs[i],
					   INOTIRY_WATCH_MASK);
		if (wds[i] < 0) {
			_error("inotify_add_watch error at %d\n", i);
			goto unwatch_and_free_wds;
		}
		wd_target_name[wds[i]] = target_dirs[i];

		_debug("Monitoring '%s'\n", target_dirs[i]);
	}

	while (1) {
		len = read(fd, event_buf, EVENT_BUF_LEN);
		bzero(&file_table, sizeof(struct trans_file_table));
		
		_debug("read return\n");

		for (offset = 0, i = 0; offset < len && i < MAX_FILE_ENTRIES;
		     offset += EVENT_LEN + event->len, i++) {
			char *target_dir;
			struct trans_file_entry *file_e = file_table.entries + i;
			event = (struct inotify_event *)(event_buf + offset);
			target_dir = wd_target_name[event->wd];

			if (event->len)
				sprintf(file_e->name, "%s/%s",
						target_dir, event->name);
			else
				strcpy(file_e->name, target_dir);
			file_e->file_type = event->mask & IN_ISDIR ?
						DIRECTORY : REGULAR;
			get_operation_type(file_e, event, target_dir);

			/* if it's delete operation, no need to get timestamp */
			if (file_e->op_type == FILE_DELETE) 
				continue;

			ret = get_timestamp(file_e);
			if (ret < 0) {
				_debug("Get timestamp for '%s' failed, skip\n",
						file_e->name);
				i--;
				continue;
			}
		}

		file_table.n = i;
	}

unwatch_and_free_wds:
	unwatch_targets(fd, wds, target_n);
close_fd:
	close(fd);
free_target_dirs:
	free_target_dirs(target_dirs, target_n);
	return ret;
}
