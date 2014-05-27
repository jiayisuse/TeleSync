#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#include <hash.h>
#include <debug.h>
#include <packet_def.h>
#include "file_table.h"


static bool already_in_owners(struct list_head *head, struct peer_id_list *owner)
{
	struct list_head *pos;
	list_for_each(pos, head) {
		struct peer_id_list *p = list_entry(pos, struct peer_id_list, l);
		if (p->ip == owner->ip)
			return true;
	}
	return false;
}

/**
 * add all the owners in trans_file_entry *te to a linked list head
 * @owner_h: the linked list head to be added
 * @te: the trans_file_entry in which the owners would be added to
 *      the new list
 */
static void __peer_id_list_add(struct list_head *owner_h,
			     struct trans_file_entry *te)
{
	int i;

	if (owner_h == NULL || te == NULL)
		return;

	for (i = 0; i < te->owner_n; i++) {
		struct peer_id_list *p = calloc(1, sizeof(struct peer_id_list));
		if (p == NULL) {
			_error("peer_id_list alloc failed\n");
			break;
		}
		INIT_LIST_ELM(&p->l);
		p->ip = te->owners[i].ip;
		p->port = te->owners[i].port;
		if (!already_in_owners(owner_h, p))
			list_add_tail(owner_h, &p->l);
	}
}

/**
 * unlink and free all the owners in the linked list
 * @head: the linked list head that points to all the woners
 */
static inline void __peer_id_list_destroy(struct list_head *head)
{
	struct list_head *pos, *tmp;

	if (head == NULL)
		return;

	list_for_each_safe(pos, tmp, head) {
		struct peer_id_list *p = list_entry(pos, struct peer_id_list, l);
		list_del(&p->l);
		free(p);
	}
}

static inline void __peer_id_list_replace(struct file_entry *fe,
					  struct trans_file_entry *te)
{
	__peer_id_list_destroy(&fe->owner_head);
	__peer_id_list_add(&fe->owner_head, te);
}

inline void peer_id_list_replace(struct file_entry *fe,
			  	 struct trans_file_entry *te)
{
	pthread_rwlock_wrlock(&fe->rwlock);
	__peer_id_list_replace(fe, te);
	pthread_rwlock_unlock(&fe->rwlock);
}


/**
 * file entry operations
 */

/**
 * file entry allocation which would do some default initialization
 */
struct file_entry *file_entry_alloc()
{
	struct file_entry *fe = calloc(1, sizeof(struct file_entry));
	if (fe == NULL)
		return NULL;
	INIT_LIST_HEAD(&fe->owner_head);
	INIT_HLIST_NODE(&fe->hlist);
	pthread_rwlock_init(&fe->rwlock, NULL);
	fe->status = FILE_AVAILABLE;

	return fe;
}

/**
 * link the file entry to the file table
 * You MUST lock the mutex of the table before calling it
 * @table: the file table that would be added to
 * @fe: the file entry than would be added to the tale
 */
inline void file_entry_add(struct file_table *table, struct file_entry *fe)
{
	if (table == NULL || fe == NULL)
		return;

	pthread_rwlock_wrlock(&fe->rwlock);
	hash_add(table->file_htable, &fe->hlist, ELFhash(fe->name));
	pthread_rwlock_unlock(&fe->rwlock);
	table->n++;
}

/**
 * fill the file entry from trans file entry
 * @fe: the file entry which to be filled
 * @te: the trans file entry which would be fill from
 */
void file_entry_fill_from(struct file_entry *fe, struct trans_file_entry *te)
{
	if (fe == NULL || te == NULL)
		return;

	pthread_rwlock_wrlock(&fe->rwlock);
	strcpy(fe->name, te->name);
	fe->timestamp = te->timestamp;
	fe->type = te->file_type;
	__peer_id_list_add(&fe->owner_head, te);
	pthread_rwlock_unlock(&fe->rwlock);
}

/**
 * fill the trans file entry from file entry
 * @fe: the trans file entry which to be filled
 * @te: the file entry which would be fill from
 */
void trans_entry_fill_from(struct trans_file_entry *te, struct file_entry *fe)
{
	struct list_head *pos;

	if (te == NULL || fe == NULL)
		return;

	pthread_rwlock_rdlock(&fe->rwlock);
	bzero(te, sizeof(struct trans_file_entry));
	strcpy(te->name, fe->name);
	te->timestamp = fe->timestamp;
	te->file_type = fe->type;
	list_for_each(pos, &fe->owner_head) {
		struct peer_id_list *p = list_entry(pos, struct peer_id_list, l);
		te->owners[te->owner_n].ip = p->ip;
		te->owners[te->owner_n].port = p->port;
		te->owner_n++;
	}
	pthread_rwlock_unlock(&fe->rwlock);
}

/**
 * update file entry according to the trans file entry information
 * @fe: the file entry which is going to be updated
 * @te: the trans file entry which is used to do the update
 * return: 0 if succeds, otherwise -1
 */
int file_entry_update(struct file_entry *fe, struct trans_file_entry *te)
{
	int ret = 0;

	if (fe == NULL || te == NULL)
		return -1;

	pthread_rwlock_wrlock(&fe->rwlock);
	if (te->timestamp > fe->timestamp) {
		fe->timestamp = te->timestamp;
		__peer_id_list_replace(fe, te);
	} else if (te->timestamp == fe->timestamp)
		__peer_id_list_add(&fe->owner_head, te);
	else
		ret = -1;
	pthread_rwlock_unlock(&fe->rwlock);

	return ret;
}

inline void file_entry_update_timestamp(struct file_entry *fe,
					uint64_t timestamp)
{
	pthread_rwlock_wrlock(&fe->rwlock);
	fe->timestamp = timestamp;
	pthread_rwlock_unlock(&fe->rwlock);
}

/**
 * delete the file entry from the file table
 * @table: the file table which the file entry would be deleted from
 * @fe: the file entry which would be deleted
 */
inline void file_entry_delete(struct file_table *table, struct file_entry *fe)
{
	if (table == NULL || fe == NULL)
		return;

	pthread_rwlock_wrlock(&fe->rwlock);
	__peer_id_list_destroy(&fe->owner_head);
	hash_del(&fe->hlist);
	pthread_rwlock_unlock(&fe->rwlock);
	pthread_rwlock_destroy(&fe->rwlock);
	free(fe);

	table->n--;
}

/**
 * file table operations
 */
static inline
struct file_entry *__file_table_search(struct file_table *table, char *name)
{
	struct file_entry *fe;
	int key = ELFhash(name);

	hash_for_each_possible(table->file_htable, fe, hlist, key)
		if (strcmp(fe->name, name) == 0)
			return fe;

	return NULL;
}

/**
 * init a file table
 * @table: the file table which would be inited
 */
inline void file_table_init(struct file_table *table)
{
	if (table == NULL)
		return;

	table->n = 0;
	pthread_mutex_init(&table->mutex, NULL);
	hash_init(table->file_htable);
}

/**
 * find a file entry from the file table
 * @table: the file table which the file entry would be search from
 * @te: the trans file entry that would be searched
 * @return: the corresponding file entry with the trans file entry
 */
inline struct file_entry *file_table_find(struct file_table *table,
					  struct trans_file_entry *te)
{
	struct file_entry *fe;

	pthread_mutex_lock(&table->mutex);
	fe = __file_table_search(table, te->name);
	pthread_mutex_unlock(&table->mutex);

	return fe;
}

/**
 * add a file entry to the file table. if the entry is already there,
 * it would update it.
 * @table: the file table which the file entry would be search from
 * @te: the trans file entry that would be added to
 * @return: the newly created file entry
 */
struct file_entry *file_table_add(struct file_table *table,
				  struct trans_file_entry *te)
{
	struct file_entry *fe = NULL;
	
	if (table == NULL || te == NULL)
		return fe;

	pthread_mutex_lock(&table->mutex);
	fe = __file_table_search(table, te->name);
	if (fe == NULL) {
		fe = file_entry_alloc();
		if (fe == NULL) {
			_error("file entry alloc failed\n");
			goto out;
		}
		file_entry_fill_from(fe, te);
		file_entry_add(table, fe);
	} else
		file_entry_update(fe, te);
out:
	pthread_mutex_unlock(&table->mutex);

	return fe;
}

/**
 * update a file entry in the file table
 * @table: the file table which the file entry would be update from
 * @te: the trans file entry that would be updated
 * @return: 0 success, -1 otherwise
 */
int file_table_update(struct file_table *table, struct trans_file_entry *te)
{
	struct file_entry *fe;
	int ret = -1;

	if (table == NULL || te == NULL)
		return ret;

	pthread_mutex_lock(&table->mutex);
	fe = __file_table_search(table, te->name);
	if (fe == NULL)
		_error("file '%s' does not exist!\n", te->name);
	else {
		file_entry_update(fe, te);
		ret = 0;
	}
	pthread_mutex_unlock(&table->mutex);

	return ret;
}

/**
 * delete a file entry from the file table
 * @table: the file table which the file entry would be deleted from
 * @te: the trans file entry that would be deleted
 * @return: 0 success, -1 otherwise
 */
int file_table_delete(struct file_table *table, struct trans_file_entry *te)
{
	struct file_entry *fe;
	int ret = -1;

	if (table == NULL || te == NULL)
		return ret;

	pthread_mutex_lock(&table->mutex);
	fe = __file_table_search(table, te->name);
	if (fe != NULL) {
		file_entry_delete(table, fe);
		ret = 0;
	}
	pthread_mutex_unlock(&table->mutex);

	return ret;
}

void file_table_delete_owner(struct file_table *table, uint32_t ip)
{
	struct file_entry *fe;
	struct hlist_node *htmp;
	struct list_head *pos, *tmp;
	int i;

	pthread_mutex_lock(&table->mutex);
	hash_for_each_safe(table->file_htable, i, htmp, fe, hlist) {
		pthread_rwlock_wrlock(&fe->rwlock);
		list_for_each_safe(pos, tmp, &fe->owner_head) {
			struct peer_id_list *p = list_entry(pos,
					struct peer_id_list, l);
			if (p->ip == ip) {
				list_del(&p->l);
				free(p);
			}
		}
		pthread_rwlock_unlock(&fe->rwlock);

		if (list_empty(&fe->owner_head)) {
			hash_del(&fe->hlist);
			free(fe);
		}
	}
	pthread_mutex_unlock(&table->mutex);
}

void file_table_destroy(struct file_table *table)
{
	struct file_entry *fe;
	struct hlist_node *tmp;
	int i;

	pthread_mutex_lock(&table->mutex);
	hash_for_each_safe(table->file_htable, i, tmp, fe, hlist) {
		if (fe == NULL)
			continue;
		file_entry_delete(table, fe);
	}
	pthread_mutex_unlock(&table->mutex);
}

/**
 * destroy a file table
 * @table: the file table which would be destroyed
 */
void file_table_print(struct file_table *table)
{
	struct file_entry *fe;
	struct hlist_node *tmp;
	int i;

	printf("\n");
	_debug("-------- File Table -------\n");
	pthread_mutex_lock(&table->mutex);
	hash_for_each_safe(table->file_htable, i, tmp, fe, hlist) {
		_debug("%-60s %lu\n", fe->name, fe->timestamp);
	}
	pthread_mutex_unlock(&table->mutex);
	_debug("----------- END ----------\n\n");
}

/**
 * fill a trans file table from the file table
 * @tft: the trans file table which would be filled
 * @ft: trans file table which would be filled from
 */
void trans_table_fill_from(struct trans_file_table *tft, struct file_table *ft)
{
	struct file_entry *fe;
	struct hlist_node *tmp;
	int i;

	if (tft == NULL || ft == NULL)
		return;

	bzero(tft, sizeof(struct trans_file_table));
	pthread_mutex_lock(&ft->mutex);
	hash_for_each_safe(ft->file_htable, i, tmp, fe, hlist) {
		trans_entry_fill_from(tft->entries + tft->n, fe);
		tft->n++;
	}
	pthread_mutex_unlock(&ft->mutex);
}
 
bool has_same_owners(struct file_entry *fe, struct trans_file_entry *te)
{
	struct list_head *pos;
	int n = 0;

	if (fe == NULL || te == NULL)
		return false;

	list_for_each(pos, &fe->owner_head)
		n++;

	if (n != te->owner_n)
		return false;

	n = 0;
	list_for_each(pos, &fe->owner_head) {
		struct peer_id_list *p = list_entry(pos, struct peer_id_list, l);
		if (p->ip != te->owners[n].ip || p->port != te->owners[n].port)
			return false;
		n++;
	}

	return true;
}
