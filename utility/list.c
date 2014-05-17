#include <list.h>

static inline int listelm_detach(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;

	return LIST_SUCCESS;
}

static inline int listelm_add(struct list_head * le, struct list_head * prev, struct list_head * next)
{
	prev->next = le;
	le->prev = prev;
	le->next = next;
	next->prev = le;

	return LIST_SUCCESS;
}

int list_add(struct list_head *head, struct list_head *le)
{

	if (!head || !le)
		return LIST_NULL;

	listelm_add(le, head, head->next);

	return LIST_SUCCESS;
}

int list_add_tail(struct list_head *head, struct list_head *le)
{

	if (!head || !le)
		return LIST_NULL;

	listelm_add(le, head->prev, head);

	return LIST_SUCCESS;
}

int list_del(struct list_head *le)
{
	if (!le)
		return LIST_NULL;

	if (list_unattached(le))
		return LIST_NULL;

	listelm_detach(le->prev, le->next);

	le->next = le->prev = NULL;

	return LIST_SUCCESS;
}

int list_del_init(struct list_head *le)
{
	int ret;

	ret = list_del(le);
	le->next = le->prev = le;

	return ret;
}
