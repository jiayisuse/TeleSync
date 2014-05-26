#include <pthread.h>
#include <sys/time.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <debug.h>
#include <list.h>
#include <packet_def.h>
#include "peer_table.h"

/**
 * peer entry allocation function, which also finishes some
 * default initialization
 */

inline struct peer_entry *peer_entry_alloc()
{
	struct timeval current_time;
	struct peer_entry *pe;
	
	pe = calloc(1, sizeof(struct peer_entry));
	if (pe == NULL)
		return NULL;
	gettimeofday(&current_time, NULL);
	pe->timestamp = GET_TIMESTAMP(&current_time);
	INIT_LIST_ELM(&pe->l);
	 
	return pe;
}

/**
 * alloc and initialize an empty peer_table
 * return: a pointer to the new peer_table
 */
inline void peer_table_init(struct peer_table *pt)
{
	if (pt == NULL)
		return;

	bzero(pt, sizeof(struct peer_table));
	pthread_mutex_init(&pt->mutex, NULL);
	INIT_LIST_HEAD(&pt->peer_head);
}

/* alloc and init a new peer_table_entry, then add it to peer_table 
 * @pt: peer table
 * @ip: new peer ip
 * @port: new peer port
 * @conn: tcp connection fd with peer
 * return 1 if success, -1 otherwise
 */
int peer_table_add(struct peer_table *pt, struct peer_entry *pe)
{
	if (pt == NULL)
		return -1;

	pthread_mutex_lock(&pt->mutex);
	list_add(&pt->peer_head, &pe->l);
	pt->peer_num++;
	pthread_mutex_unlock(&pt->mutex);

	return 0;	
}

/**
 * delete the peer_table_entry entry and free the memory
 * @pt: peer_table
 * @entry: peer_table_entry
 * return 1 if success, -1 otherwise
 */
int peer_table_delete(struct peer_table *pt, int conn)
{
	struct list_head *pos;
	int ret = -1;

	if (pt == NULL)
		return ret;

	pthread_mutex_lock(&pt->mutex);
	list_for_each(pos, &pt->peer_head) {
		struct peer_entry *pe = list_entry(pos, struct peer_entry, l);
		if (pe->conn == conn) {
			ret = pe->peerid.ip;
			list_del(&pe->l);
			free(pe);
			pt->peer_num--;
			break;
		}
	}
	pthread_mutex_unlock(&pt->mutex);

	return ret;
}

/**
 * find an entry in peer table by ip (only one device for each ip)
 * return: pointer to the peer_table_entry, or return NULL if fail to find
 * such an entry
 */
struct peer_entry *peer_table_find(struct peer_table *pt, uint32_t ip)
{
	struct list_head *pos;

	if (pt == NULL)
		return NULL;

	pthread_mutex_lock(&pt->mutex);
	list_for_each(pos, &pt->peer_head) {
		struct peer_entry *pe = list_entry(pos, struct peer_entry, l);
		if (pe->peerid.ip == ip) {
			pthread_mutex_unlock(&pt->mutex);
			return pe;
		}
	}
	pthread_mutex_unlock(&pt->mutex);

	return NULL;
}


/**
 * print the content of a peer table
 */
void peer_table_print(struct peer_table *pt)
{
	struct list_head *pos;

	pthread_mutex_lock(&pt->mutex);
	_debug("{ Peer Table }:\n");
	_debug("\tnum of peers: %d\n",pt->peer_num);

	list_for_each(pos, &pt->peer_head) {
		struct peer_entry *pe = list_entry(pos, struct peer_entry, l);
		_debug("ip: %10u, port: %6d, timestamp: %10ld\n",
				pe->peerid.ip, pe->peerid.port, pe->timestamp);
	}
	pthread_mutex_unlock(&pt->mutex);
}
