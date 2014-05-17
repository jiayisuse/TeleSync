#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <arpa/inet.h>

#include <debug.h>
#include <consts.h>
#include <trans_file_table.h>
#include <list.h>
#include "start.h"
#include "packet.h"
#include "file_monitor.h"

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

static void targets_dump(struct monitor_table *table)
{
	struct list_head *pos;
	int fd = open(CLIENT_TARGET_FILE, O_RDWR | O_CREAT | O_TRUNC | O_APPEND,
			S_IRUSR | S_IWUSR);
	if (fd < 0) {
		_error("Could not open '%s'\n", CLIENT_TARGET_FILE);
		return;
	}

	list_for_each(pos, &table->head) {
		struct monitor_target *target;
		target = list_entry(pos, struct monitor_target, l);
		write(fd, target->name, strlen(target->name));
		write(fd, "\n", 1);
	}
}

static inline void unwatch_and_free_target(struct monitor_table *table,
					   struct monitor_target *target)
{
	_debug("{ Un-Watching } '%s(%d)'\n", target->name, target->wd);
	table->targets[target->wd] = NULL;
	inotify_rm_watch(table->fd, target->wd);
	list_del(&target->l);
	free(target);
}

static void unwatch_and_free_targets(struct monitor_table *table)
{
	struct list_head *pos, *tmp;
	list_for_each_safe(pos, tmp, &table->head) {
		struct monitor_target *target;
		target = list_entry(pos, struct monitor_target, l);
		unwatch_and_free_target(table, target);
	}
	close(table->fd);
	bzero(table, sizeof(struct monitor_table));
}

static int watch_target_add(struct monitor_table *table, char *name)
{
	struct monitor_target *target;

	target = calloc(1, sizeof(*target));
	if (target == NULL) {
		_error("calloc monitor target failed\n");
		return -1;
	}

	target->wd = inotify_add_watch(table->fd, name, INOTIRY_WATCH_MASK);
	if (target->wd < 0) {
		_error("inotify_add_watch error for '%s'\n", name);
		free(target);
		return -1;
	}
	strcpy(target->name, name);
	INIT_LIST_ELM(&target->l);

	/* add target to target table */
	table->targets[target->wd] = target;
	list_add_tail(&table->head, &target->l);
	table->n++;

	_debug("{ Monitoring } '%s(%d)'\n", target->name, target->wd);

	return 0;
}

static int handle_event(struct inotify_event *event,
			struct trans_file_entry *file_e,
			struct monitor_table *table)
{
	char *target_name = table->targets[event->wd]->name;
	int ret = 0;

	file_e->file_type = event->mask & IN_ISDIR ? DIRECTORY : REGULAR;

	if (event->mask & IN_CREATE) {
		file_e->op_type = FILE_ADD;
		_debug("INOTIFY: '%s' created\n", file_e->name);
	} else if (event->mask & IN_DELETE) {
		file_e->op_type = FILE_DELETE;
		_debug("INOTIFY: '%s' deleted\n", file_e->name);
	} else if (event->mask & IN_DELETE_SELF) {
		file_e->op_type = FILE_DELETE;
		_debug("INOTIFY: Target '%s' was itself deleted\n", target_name);
		unwatch_and_free_target(table, table->targets[event->wd]);
	} else if (event->mask & IN_MODIFY) {
		file_e->op_type = FILE_MODIFY;
		_debug("INOTIFY '%s' was modified\n", file_e->name);
	} else if (event->mask & IN_MOVE_SELF) {
		file_e->op_type = FILE_DELETE;
		_debug("INOTIFY: Target '%s' was itself moved", target_name);
		unwatch_and_free_target(table, table->targets[event->wd]);
	} else if (event->mask & IN_MOVED_FROM) {
		file_e->op_type = FILE_DELETE;
		_debug("INOTIFY: '%s' moved out\n", file_e->name);
	} else if (event->mask & IN_MOVED_TO) {
		file_e->op_type = FILE_ADD;
		_debug("INOTIFY: '%s' moved into\n", file_e->name);
	} else
		file_e->op_type = FILE_NONE;

	if (file_e->op_type == FILE_ADD && file_e->file_type == DIRECTORY)
		ret = watch_target_add(table, file_e->name);

	if (file_e->op_type != FILE_DELETE && file_e->op_type != FILE_NONE) {
		ret = get_timestamp(file_e);
		if (ret < 0)
			_debug("Get timestamp for '%s' failed, skip\n",
					file_e->name);
	}

	return ret;
}

static void file_monitor_cleanup(void *arg)
{
	struct monitor_table *table = arg;
	targets_dump(table);
	unwatch_and_free_targets(table);
}

void *file_monitor_task(void *arg)
{
	struct client_thread_arg *targ = arg;
	char **target_dirs;
	char event_buf[EVENT_BUF_LEN] = { 0 };
	struct inotify_event *event;
	struct trans_file_table file_table;
	struct ptot_packet packet;
	struct monitor_table m_table;
	int i, offset, target_n, conn;
	long int ret = -1;
	ssize_t len;

	target_n = targ->conf.target_n;
	target_dirs = targ->conf.target_dirs;
	conn = targ->conn;

	bzero(&m_table, sizeof(struct monitor_table));
	INIT_LIST_HEAD(&m_table.head);
	m_table.fd = inotify_init();
	if (m_table.fd < 0) {
		_error("inotify_init failed\n");
		goto out;
	}

	for (i = 0; i < target_n; i++) {
		if (watch_target_add(&m_table, target_dirs[i]) != 0) {
			pthread_wait_notify(&targ->wait, THREAD_FAILED);
			goto unwatch_and_free_mtable;
		}
	}

	pthread_wait_notify(&targ->wait, THREAD_RUNNING);
	pthread_cleanup_push(file_monitor_cleanup, &m_table);

	while (1) {
		len = read(m_table.fd, event_buf, EVENT_BUF_LEN);
		bzero(&file_table, sizeof(struct trans_file_table));
		
		for (offset = 0, i = 0; offset < len && i < MAX_FILE_ENTRIES;
		     offset += EVENT_LEN + event->len, i++) {
			char *target_dir;
			struct trans_file_entry *file_e = file_table.entries + i;

			event = (struct inotify_event *)(event_buf + offset);
			if (m_table.targets[event->wd] == NULL)
				continue;
			target_dir = m_table.targets[event->wd]->name;
			if (event->len)
				sprintf(file_e->name, "%s/%s",
						target_dir, event->name);
			else
				strcpy(file_e->name, target_dir);

			if (handle_event(event, file_e, &m_table) != 0)
				i--;
		}
		file_table.n = i;

		ptot_packet_init(&packet, PEER_FILE_UPDATE);
		ptot_packet_fill(&packet, &file_table, trans_table_len(&file_table));
		send_ptot_packet(conn, &packet);
	}

	pthread_cleanup_pop(0);

unwatch_and_free_mtable:
	unwatch_and_free_targets(&m_table);
out:
	pthread_exit((void *)ret);
}
