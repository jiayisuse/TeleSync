#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
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



static struct monitor_table m_table;
static struct ptot_packet pkt;
static struct trans_file_table file_table;


inline static int get_timestamp(struct trans_file_entry *file_e, char *name)
{
	struct stat st;
	if (stat(name, &st) < 0) {
		perror("stat error");
		return -1;
	} else {
		file_e->timestamp = st.st_mtime;
		return 0;
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

static int watch_target_add(struct monitor_table *table,
			    char *name, char *local_name)
{
	struct monitor_target *target;

	target = calloc(1, sizeof(*target));
	if (target == NULL) {
		_error("calloc monitor target failed\n");
		return -1;
	}

	target->wd = inotify_add_watch(table->fd, name, DEFAULT_WATCH_MASK);
	if (target->wd < 0) {
		_debug("inotify_add_watch error for '%s'\n", name);
		free(target);
		return -1;
	}
	strcpy(target->name, name);
	strcpy(target->local_name, local_name);
	INIT_LIST_ELM(&target->l);

	/* add target to target table */
	table->targets[target->wd] = target;
	list_add_tail(&table->head, &target->l);
	table->n++;

	_debug("{ Monitoring } '%s(%d)'\n", target->name, target->wd);

	return 0;
}

static int watch_target_add_dir(struct monitor_table *table,
				char *dir, char *local_dir)
{
	DIR *root;
	struct dirent *d;
	int ret = 0;

	if (dir == NULL)
		return -1;

	watch_target_add(table, dir, local_dir);

	if ((root = opendir(dir)) == NULL) {
		_error("target '%s' open failed\n", dir);
		return -1;
	}

	while ((d = readdir(root)) != NULL) {
		if (d->d_type == DT_DIR) {
			char new_dir[MAX_NAME_LEN];
			char new_local_dir[MAX_NAME_LEN];
			sprintf(new_dir, "%s/%s", dir, d->d_name);
			sprintf(new_local_dir, "%s/%s", local_dir, d->d_name);
			if (strcmp(d->d_name, ".") == 0 ||
					strcmp(d->d_name, "..") == 0)
				continue;
			ret = watch_target_add_dir(table, new_dir, new_local_dir)
				|| ret;
		}
	}
	closedir(root);

	return ret;
}

static int handle_event(struct inotify_event *event,
			struct trans_file_entry *file_e,
			char *file_name)
{
	char *target_name = m_table.targets[event->wd]->name;
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
		unwatch_and_free_target(&m_table, m_table.targets[event->wd]);
	} else if (event->mask & IN_MODIFY) {
		file_e->op_type = FILE_MODIFY;
		//_debug("INOTIFY '%s' was modified\n", file_e->name);
	} else if (event->mask & IN_MOVE_SELF) {
		file_e->op_type = FILE_DELETE;
		_debug("INOTIFY: Target '%s' was itself moved", target_name);
		unwatch_and_free_target(&m_table, m_table.targets[event->wd]);
	} else if (event->mask & IN_MOVED_FROM) {
		file_e->op_type = FILE_DELETE;
		_debug("INOTIFY: '%s' moved out\n", file_e->name);
	} else if (event->mask & IN_MOVED_TO) {
		file_e->op_type = FILE_ADD;
		_debug("INOTIFY: '%s' moved into\n", file_e->name);
	} else {
		file_e->op_type = FILE_NONE;
		ret = -1;
		_debug("INOTIFY: '%s' UN-Known\n", file_e->name);
	}

	if (file_e->op_type == FILE_ADD && file_e->file_type == DIRECTORY)
		ret = watch_target_add(&m_table, file_name, file_e->name);

	if (file_e->op_type != FILE_DELETE && file_e->op_type != FILE_NONE) {
		ret = get_timestamp(file_e, file_name);
		if (ret < 0)
			_debug("Get timestamp for '%s' failed, skip\n",
					file_e->name);
	}

	return ret;
}

static void file_monitor_cleanup(void *arg)
{
	unwatch_and_free_targets(&m_table);
}

/*
static void print_file_table()
{
	int i = 0;
	printf("N = %d\n", file_table.n);
	for (i = 0; i < file_table.n; i++)
		printf("%-60s OP(#%d)\n", file_table.entries[i].name,
				file_table.entries[i].op_type);
}
*/

/**
 * tell inotify not monitor add and modify operations.
 * You MUST call it just before creating a new file
 * @return: the monitor target, which should be used to call
 *          file_monitor_unblock() later
 */
struct monitor_target *file_monitor_block(char *file_name)
{
	struct list_head *pos;
	char *index = rindex(file_name, '/');

	*index = '\0';
	list_for_each(pos, &m_table.head) {
		struct monitor_target *t = list_entry(pos,
					struct monitor_target, l);
		if (strcmp(t->name, file_name) == 0) {
			t->wd = inotify_add_watch(m_table.fd, t->name,
					BLOCK_CREATE_MAST);
			*index = '/';
			return t;
		}
	}

	*index = '/';
	return NULL;
}

/**
 * tell inotify to monitor add and modify operations.
 * You MUST call it immediately when you finish wrting file
 * @target: the monitor target pointer returned by
 *          file_monitor_block()
 */
inline void file_monitor_unblock(struct monitor_target *target)
{
	if (target == NULL)
		return;

	target->wd = inotify_add_watch(m_table.fd, target->name,
			DEFAULT_WATCH_MASK);
}

void *file_monitor_task(void *arg)
{
	struct client_thread_arg *targ = arg;
	char **target_dirs;
	char event_buf[EVENT_BUF_LEN] = { 0 };
	struct inotify_event *event;
	int i, offset, target_n, conn;
	long int ret = 1;
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
		char local_dir[MAX_NAME_LEN];
		sprintf(local_dir, "%d", i + 1);
		ret = watch_target_add_dir(&m_table, target_dirs[i], local_dir)
			&& ret;
	}

	if (ret != 0) {
		pthread_wait_notify(&targ->wait, THREAD_FAILED);
		goto unwatch_and_free_mtable;
	}
	pthread_wait_notify(&targ->wait, THREAD_RUNNING);
	pthread_cleanup_push(file_monitor_cleanup, NULL);

	bzero(&file_table, sizeof(struct trans_file_table));
	while (1) {
		usleep(100000);

		len = read(m_table.fd, event_buf, EVENT_BUF_LEN);
		bzero(&file_table, sizeof(file_table));
		
		for (offset = 0;
		     offset < len && file_table.n < MAX_FILE_ENTRIES;
		     offset += EVENT_LEN + event->len, file_table.n++) {
			char *dir, *local_dir, new_name[MAX_NAME_LEN];
			struct trans_file_entry *file_e =
				file_table.entries + file_table.n;

			event = (struct inotify_event *)(event_buf + offset);
			if (m_table.targets[event->wd] == NULL) {
				file_table.n--;
				continue;
			}
			local_dir = m_table.targets[event->wd]->local_name;
			dir = m_table.targets[event->wd]->name;
			if (event->len) {
				sprintf(file_e->name, "%s/%s",
						local_dir, event->name);
				sprintf(new_name, "%s/%s", dir, event->name);
			} else {
				strcpy(file_e->name, local_dir);
				strcpy(new_name, event->name);
			}

			if (handle_event(event, file_e, new_name) != 0)
				file_table.n--;
		}

		ptot_packet_init(&pkt, PEER_FILE_UPDATE);
		ptot_packet_fill(&pkt, &file_table, trans_table_len(&file_table));
		send_ptot_packet(conn, &pkt);
	}

	pthread_cleanup_pop(0);

unwatch_and_free_mtable:
	unwatch_and_free_targets(&m_table);
out:
	pthread_exit((void *)ret);
}
