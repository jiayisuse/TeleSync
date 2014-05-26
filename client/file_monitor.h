#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

#include <file_table.h>
#include <list.h>

#define DEFAULT_WATCH_MASK	(IN_CREATE | IN_DELETE | IN_DELETE_SELF |	\
				 IN_MODIFY | IN_MOVE_SELF |			\
				 IN_MOVED_FROM | IN_MOVED_TO)
#define BLOCK_CREATE_MASK	(IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF |	\
				 IN_MOVED_FROM | IN_MOVED_TO)
#define BLOCK_ALL_MASK		(IN_DELETE_SELF | IN_MOVE_SELF)
#define EVENT_LEN		(sizeof(struct inotify_event ))
#define EVENT_BUF_LEN		(1024 * (EVENT_LEN + 16))
#define WD_HASH_SIZE		128

struct monitor_target {
	int wd;
	char sys_name[MAX_NAME_LEN];
	char logic_name[MAX_NAME_LEN];
	struct list_head l;
	pthread_mutex_t mutex;
};

struct monitor_table {
	int fd;
	int n;
	struct list_head head;
	struct monitor_target *targets[WD_HASH_SIZE];
	pthread_mutex_t mutex;
};

void *file_monitor_task(void *arg);

int get_file_table(struct file_table *ft, char **target, int n);

struct monitor_target *file_monitor_block(char *file_name);
void file_monitor_unblock(struct monitor_target *target);
char *get_sys_name(char *logic_name);
char *monitor_get_sys_name(char *logic_name, struct monitor_target *target);
int file_monitor_mkdir(const char *sys_name, const char *logic_name);
void file_change_modtime(const char *sys_name, uint64_t modtime);

#endif
