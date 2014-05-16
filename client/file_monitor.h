#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

#include <utility/list.h>

#define INOTIRY_WATCH_MASK	(IN_CREATE | IN_DELETE | IN_DELETE_SELF |	\
				 IN_MODIFY | IN_MOVE_SELF |			\
				 IN_MOVED_FROM | IN_MOVED_TO)
#define EVENT_LEN		(sizeof(struct inotify_event ))
#define EVENT_BUF_LEN		(1024 * (EVENT_LEN + 16))
#define WD_HASH_SIZE		128

struct monitor_target {
	int wd;
	char name[MAX_NAME_LEN];
	struct list_head l;
};

struct monitor_table {
	int fd;
	int n;
	struct list_head head;
	struct monitor_target *targets[WD_HASH_SIZE];
};

void *file_monitor_task(void *arg);

#endif
