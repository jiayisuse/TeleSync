#include <pthread.h>
#include <utility/pthread_wait.h>

void pthread_wait_init(struct pthread_wait_t *wait)
{
	pthread_mutex_init(&wait->mutex, NULL);
	pthread_cond_init(&wait->cond, NULL);
	wait->status = THREAD_FAILED;
}

void pthread_wait_for(struct pthread_wait_t *wait)
{
	pthread_mutex_lock(&wait->mutex);
	pthread_cond_wait(&wait->cond, &wait->mutex);
	pthread_mutex_unlock(&wait->mutex);
}

void pthread_wait_notify(struct pthread_wait_t *wait, enum thread_status status)
{
	pthread_mutex_lock(&wait->mutex);
	wait->status = status;
	pthread_cond_signal(&wait->cond);
	pthread_mutex_unlock(&wait->mutex);
}
