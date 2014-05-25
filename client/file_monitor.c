#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>
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


extern uint32_t my_ip;
extern struct file_table ft;

static struct monitor_table m_table;
static struct ptot_packet pkt;
static struct trans_file_table tft;


inline static int get_trans_timestamp(struct trans_file_entry *te, char *name)
{
	struct stat st;
	if (stat(name, &st) < 0) {
		perror("stat error");
		return -1;
	} else {
		te->timestamp = st.st_mtime;
		return 0;
	}
}

inline static int get_file_timestamp(struct file_entry *fe, char *name)
{
	struct stat st;
	if (stat(name, &st) < 0) {
		perror("stat error");
		return -1;
	} else {
		fe->timestamp = st.st_mtime;
		return 0;
	}
}

static inline void unwatch_and_free_target(struct monitor_table *table,
					   struct monitor_target *target)
{
	_debug("{ Un-Watching } '%s(%d)'\n", target->logic_name, target->wd);
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

static struct monitor_target *watch_target_add(struct monitor_table *table,
					       const char *sys_name,
					       const char *logic_name)
{
	struct monitor_target *target;
	int wd;

	wd = inotify_add_watch(table->fd, sys_name, DEFAULT_WATCH_MASK);
	if (wd < 0) {
		_debug("inotify_add_watch error for '%s'\n", sys_name);
		return NULL;
	}

	if (table->targets[wd] != NULL)
		return table->targets[wd];

	target = calloc(1, sizeof(*target));
	if (target == NULL) {
		_error("calloc monitor target failed\n");
		return NULL;
	}

	strcpy(target->sys_name, sys_name);
	strcpy(target->logic_name, logic_name);
	INIT_LIST_ELM(&target->l);
	pthread_mutex_init(&target->mutex, NULL);
	target->wd = wd;

	/* add target to target table */
	table->targets[target->wd] = target;
	list_add_tail(&table->head, &target->l);
	table->n++;

	_debug("{ Monitoring } '%s(%d)'\n", target->logic_name, target->wd);

	return target;
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
			struct trans_file_entry *te,
			char *sys_name)
{
	char *target_name = m_table.targets[event->wd]->sys_name;
	int ret = 0;

	te->file_type = event->mask & IN_ISDIR ? DIRECTORY : REGULAR;

	if (event->mask & IN_CREATE) {
		te->op_type = FILE_ADD;
		_debug("INOTIFY: '%s' created\n", te->name);
	} else if (event->mask & IN_DELETE) {
		te->op_type = FILE_DELETE;
		_debug("INOTIFY: '%s' deleted\n", te->name);
	} else if (event->mask & IN_DELETE_SELF) {
		te->op_type = FILE_DELETE;
		_debug("INOTIFY: Target '%s' was itself deleted\n", target_name);
		unwatch_and_free_target(&m_table, m_table.targets[event->wd]);
	} else if (event->mask & IN_MODIFY) {
		te->op_type = FILE_MODIFY;
		_debug("INOTIFY '%s' was modified\n", te->name);
	} else if (event->mask & IN_MOVE_SELF) {
		te->op_type = FILE_DELETE;
		_debug("INOTIFY: Target '%s' was itself moved", target_name);
		unwatch_and_free_target(&m_table, m_table.targets[event->wd]);
	} else if (event->mask & IN_MOVED_FROM) {
		te->op_type = FILE_DELETE;
		_debug("INOTIFY: '%s' moved out\n", te->name);
	} else if (event->mask & IN_MOVED_TO) {
		te->op_type = FILE_ADD;
		_debug("INOTIFY: '%s' moved into\n", te->name);
	} else {
		te->op_type = FILE_NONE;
		ret = -1;
		_debug("INOTIFY: '%s' UN-Known\n", te->name);
	}

	if (te->op_type == FILE_ADD && te->file_type == DIRECTORY)
		watch_target_add(&m_table, sys_name, te->name);

	if (te->op_type != FILE_DELETE && te->op_type != FILE_NONE) {
		ret = get_trans_timestamp(te, sys_name);
		if (ret < 0)
			_debug("Get timestamp for '%s' failed, skip\n",
					te->name);
	}

	/* fill a owner to the trans file entry */
	te->owners[te->owner_n].ip = my_ip;
	te->owners[te->owner_n].port = P2P_PORT;
	te->owner_n++;

	/* update the file table */
	switch (te->op_type) {
	case FILE_ADD:
		file_table_add(&ft, te);
		break;
	case FILE_MODIFY:
		file_table_update(&ft, te);
	case FILE_DELETE:
		file_table_delete(&ft, te);
	default:
		break;
	}

	return ret;
}

static void file_monitor_cleanup(void *arg)
{
	unwatch_and_free_targets(&m_table);
}

/*
static void print_tft()
{
	int i = 0;
	printf("N = %d\n", tft.n);
	for (i = 0; i < tft.n; i++)
		printf("%-60s OP(#%d)\n", tft.entries[i].name,
				tft.entries[i].op_type);
}
*/

static int add_me_to_peer_id_list(struct list_head *head)
{
	struct peer_id_list *p = calloc(1, sizeof(struct peer_id_list));
	if (p == NULL) {
		_error("peer id list alloc failed\n");
		return -1;
	}
	p->ip = my_ip;
	p->port = P2P_PORT;
	INIT_LIST_ELM(&p->l);
	list_add(head, &p->l);
	return 0;
}

static int get_file_table_r(struct file_table *ft,
			    char *sys_name,
			    char *logic_name)
{
	DIR *root;
	struct dirent *d;
	int ret = 0;

	if ((root = opendir(sys_name)) == NULL) {
		_error("target '%s' open failed\n", sys_name);
		return -1;
	}

	while ((d = readdir(root)) != NULL) {
		char new_sys_name[MAX_NAME_LEN];
		struct file_entry *fe = file_entry_alloc();
		if (fe == NULL) {
			_error("file entry alloc failed\n");
			return -1;
		}

		if (strcmp(d->d_name, ".") != 0 &&
				strcmp(d->d_name, "..") != 0) {
			sprintf(fe->name, "%s/%s", logic_name, d->d_name);
			sprintf(new_sys_name, "%s/%s", sys_name, d->d_name);
			get_file_timestamp(fe, sys_name);
			add_me_to_peer_id_list(&fe->owner_head);
			file_entry_add(ft, fe);
		}

		if (d->d_type == DT_DIR) {
			fe->type = DIRECTORY;
			if (strcmp(d->d_name, ".") == 0 ||
					strcmp(d->d_name, "..") == 0)
				continue;
			ret = get_file_table_r(ft, new_sys_name, fe->name) || ret;
		} else
			fe->type = REGULAR;
	}

	return ret;
}

int get_file_table(struct file_table *ft, char **target, int n)
{
	int i;
	char logic_name[MAX_NAME_LEN];

	for (i = 0; i < n; i++) {
		sprintf(logic_name, "%d", i + 1);
		if (get_file_table_r(ft, target[i], logic_name) < 0)
			return -1;
	}

	return 0;
}

static inline void __file_monitor_block(struct monitor_target *t)
{
	if (t == NULL)
		return;

	t->wd = inotify_add_watch(m_table.fd, t->sys_name, BLOCK_CREATE_MAST);
}

static inline void __file_monitor_unblock(struct monitor_target *target)
{
	if (target == NULL)
		return;

	target->wd = inotify_add_watch(m_table.fd, target->sys_name,
			DEFAULT_WATCH_MASK);
}

/**
 * tell inotify not monitor add and modify operations. if no
 * monitor target found, it will create it.
 * You MUST call it just before creating a new file
 * @return: the monitor target, which should be used to call
 *          file_monitor_unblock() later
 */
struct monitor_target *file_monitor_block(char *logic_name)
{
	struct list_head *pos;
	char *last_i = rindex(logic_name, '/');
	char *first_i = index(logic_name, '/');
	char *p;
	char sys_dir[MAX_NAME_LEN], logic_dir[MAX_NAME_LEN];
	struct monitor_target *t;

	pthread_mutex_lock(&m_table.mutex);

	*last_i = '\0';
	list_for_each(pos, &m_table.head) {
		t= list_entry(pos, struct monitor_target, l);
		if (strcmp(t->logic_name, logic_name) == 0) {
			t->wd = inotify_add_watch(m_table.fd, t->sys_name,
					BLOCK_CREATE_MAST);
			*last_i = '/';
			pthread_mutex_unlock(&m_table.mutex);
			goto out;
		}
	}
	*last_i = '/';

	/* no target found, we need to create the new dir and
	   watch it */
	for ( ; first_i != last_i; first_i = index(first_i + 1, '/')) {
		*first_i = '\0';
		list_for_each(pos, &m_table.head) {
			struct monitor_target *tmp =
				list_entry(pos, struct monitor_target, l);
			if (strcmp(tmp->logic_name, logic_name) == 0) {
				t = tmp;
				break;
			}
		}
		*first_i = '/';
	}
	
	for (p = logic_name + strlen(t->logic_name) + 1, first_i = index(p, '/');
			first_i != NULL;
			p = first_i + 1, first_i = index(first_i + 1, '/')) {
		*first_i = '\0';
		sprintf(sys_dir, "%s/%s", t->sys_name, p);
		sprintf(logic_dir, "%s/%s", t->logic_name, p);

		__file_monitor_block(t);
		mkdir(sys_dir, S_IRWXU);
		__file_monitor_unblock(t);
		t = watch_target_add(&m_table, sys_dir, logic_dir);

		*first_i = '/';
	}
	pthread_mutex_unlock(&m_table.mutex);

	__file_monitor_block(t);
	pthread_mutex_lock(&t->mutex);

out:
	_leave();
	return t;
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

	target->wd = inotify_add_watch(m_table.fd, target->sys_name,
			DEFAULT_WATCH_MASK);
	pthread_mutex_unlock(&target->mutex);
}

/**
 * get sys name from the logic name
 * You MUST call it when you are going to operate file system
 * @target: the monitor target pointer returned by
 *          file_monitor_block()
 * @return: the sys name, which MUST be freed after using it
 */
char *get_sys_name(char *logic_name, struct monitor_target *target)
{
	char *sys_name;
	char *file = rindex(logic_name, '/');

	sys_name = calloc(1, MAX_NAME_LEN);
	if (sys_name == NULL) {
		_error("sys name alloc failed\n");
		return NULL;
	}

	sprintf(sys_name, "%s%s", target->sys_name, file);

	return sys_name;
}

/**
 * make a new directory and add this directory to file monitor list
 * @sys_name: the directory name in file system
 */
void file_monitor_mkdir(const char *sys_name, const char *logic_name)
{
	mkdir(sys_name, S_IRWXU);
	pthread_mutex_lock(&m_table.mutex);
	watch_target_add(&m_table, sys_name, logic_name);
	pthread_mutex_unlock(&m_table.mutex);
}

/**
 * set the file's modify time to a certain modtime, as well as
 * the access time
 * @modtime: the modify timestamp would the file be set to
 */
inline void file_change_modtime(const char *sys_name, uint64_t modtime)
{
	struct utimbuf timbuf;
	timbuf.actime = timbuf.modtime = modtime;
	utime(sys_name, &timbuf);
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
	pthread_mutex_init(&m_table.mutex, NULL);
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

	bzero(&tft, sizeof(struct trans_file_table));
	while (1) {
		usleep(100000);

		len = read(m_table.fd, event_buf, EVENT_BUF_LEN);
		bzero(&tft, sizeof(tft));
		
		for (offset = 0;
		     offset < len && tft.n < MAX_FILE_ENTRIES;
		     offset += EVENT_LEN + event->len, tft.n++) {
			char *sys_dir, *logic_dir, new_name[MAX_NAME_LEN];
			struct trans_file_entry *te= tft.entries + tft.n;

			event = (struct inotify_event *)(event_buf + offset);
			if (m_table.targets[event->wd] == NULL) {
				tft.n--;
				continue;
			}
			logic_dir = m_table.targets[event->wd]->logic_name;
			sys_dir = m_table.targets[event->wd]->sys_name;
			if (event->len) {
				sprintf(te->name, "%s/%s",
						logic_dir, event->name);
				sprintf(new_name, "%s/%s",
						sys_dir, event->name);
			} else {
				strcpy(te->name, logic_dir);
				strcpy(new_name, event->name);
			}

			if (handle_event(event, te, new_name) != 0)
				tft.n--;
		}

		ptot_packet_init(&pkt, PEER_FILE_UPDATE);
		ptot_packet_fill(&pkt, &tft, trans_table_len(&tft));
		send_ptot_packet(conn, &pkt);
	}

	pthread_cleanup_pop(0);

unwatch_and_free_mtable:
	unwatch_and_free_targets(&m_table);
out:
	pthread_exit((void *)ret);
}