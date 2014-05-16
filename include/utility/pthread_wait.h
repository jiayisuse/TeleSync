#ifndef PTHREAD_WAIT_H
#define PTHREAD_WAIT_H

#include <pthread.h>

enum thread_status {
	THREAD_FAILED,
	THREAD_RUNNING,
	THREAD_TERMINATED
};

struct pthread_wait_t {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	enum thread_status status;
};

void pthread_wait_init(struct pthread_wait_t *wait);
void pthread_wait_for(struct pthread_wait_t *wait);
void pthread_wait_notify(struct pthread_wait_t *wait, enum thread_status status);

#endif
