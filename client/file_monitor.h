#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

#define CONF_FILE		"./client/client.conf"
#define INOTIRY_WATCH_MASK	(IN_CREATE | IN_DELETE | IN_DELETE_SELF |	\
				 IN_MODIFY | IN_MOVE_SELF |			\
				 IN_MOVED_FROM | IN_MOVED_TO)
#define EVENT_LEN		(sizeof(struct inotify_event ))
#define EVENT_BUF_LEN		(1024 * (EVENT_LEN + 16))
#define WD_HASH_SIZE		128

int file_monitor_task();

#endif
