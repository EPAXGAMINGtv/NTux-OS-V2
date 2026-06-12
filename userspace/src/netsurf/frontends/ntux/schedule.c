#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <syscall.h>
#include "utils/errors.h"
#include "utils/log.h"
#include "ntux/schedule.h"

struct nscallback {
	struct nscallback *next;
	struct nscallback *prev;
	int64_t timeout;
	void (*callback)(void *p);
	void *p;
};

static struct nscallback *callback_list = NULL;

static int64_t get_ms(void)
{
	uint64_t ticks = sys_get_ticks();
	long hz = sys_get_timer_hz();
	if (hz <= 0) hz = 1000;
	return (int64_t)(ticks * 1000 / hz);
}

nserror monkey_schedule(int t, void (*callback)(void *p), void *p)
{
	struct nscallback *cb = callback_list;

	if (t < 0) {
		while (cb) {
			if (cb->callback == callback && cb->p == p) {
				cb->callback = NULL;
				return NSERROR_OK;
			}
			cb = cb->next;
		}
		return NSERROR_OK;
	}

	cb = calloc(1, sizeof(*cb));
	if (cb == NULL) return NSERROR_NOMEM;

	cb->timeout = get_ms() + t;
	cb->callback = callback;
	cb->p = p;
	cb->next = callback_list;
	if (callback_list) callback_list->prev = cb;
	callback_list = cb;

	return NSERROR_OK;
}

int monkey_schedule_run(void)
{
	struct nscallback *cb = callback_list;
	int64_t now = get_ms();
	int64_t next = -1;

	while (cb) {
		struct nscallback *n = cb->next;
		if (cb->callback == NULL) {
			if (cb->prev) cb->prev->next = cb->next;
			if (cb->next) cb->next->prev = cb->prev;
			if (callback_list == cb) callback_list = cb->next;
			free(cb);
		} else if (cb->timeout <= now) {
			void (*cb_call)(void *) = cb->callback;
			void *cb_p = cb->p;
			cb->callback = NULL;
			cb_call(cb_p);
		} else {
			int64_t diff = cb->timeout - now;
			if (next == -1 || diff < next)
				next = diff;
		}
		cb = n;
	}
	return (int)next;
}
