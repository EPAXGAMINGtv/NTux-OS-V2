#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <window.h>
#include <syscall.h>

#include "utils/config.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/filepath.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "netsurf/browser_window.h"
#include "content/fetch.h"

#include "ntux/browser.h"
#include "ntux/bitmap.h"
#include "ntux/layout.h"
#include "ntux/fetch.h"
#include "ntux/schedule.h"

static bool ntux_done = false;

static void die(const char * const error)
{
	fprintf(stderr, "NetSurf: %s\n", error);
	for (;;);
}

static struct gui_misc_table ntux_misc_table = {
	.schedule = monkey_schedule,
};

static void ntux_run(void)
{
	int prev_left = 0;
	int redraw_counter = 0;

	while (!ntux_done) {
		monkey_schedule_run();

		struct gui_window *gw = ntux_find_window_by_num(0);
		if (gw != NULL) {
			window_input_state_t st;
			window_get_input_state(gw->ntux_win, &st);
			if (window_should_close(gw->ntux_win)) {
				ntux_done = true;
				break;
			}
			if (prev_left != st.mouse_left) {
				if (st.mouse_left) {
					browser_mouse_state mouse = BROWSER_MOUSE_CLICK_1;
					browser_window_mouse_click(gw->bw, mouse,
								     st.mouse_x, st.mouse_y);
				}
				prev_left = st.mouse_left;
			}
			if (++redraw_counter >= 30) {
				redraw_counter = 0;
				ntux_redraw_window(gw);
			}
		}

		sys_wait_ticks(1);
	}
}

void ntux_user_entry(void)
{
	fprintf(stderr, "NetSurf: Initializing...\n");
	nserror ret;
	char *messages;
	int argc = 0;
	char *argv[] = { "netsurf", NULL };

	struct netsurf_table ntux_table = {
		.misc = &ntux_misc_table,
		.window = ntux_window_table,
		.fetch = ntux_fetch_table,
		.bitmap = ntux_bitmap_table,
		.layout = ntux_layout_table,
	};

	ret = netsurf_register(&ntux_table);
	if (ret != NSERROR_OK) {
		die("NetSurf operation table failed registration");
	}

nslog_init(NULL, &argc, argv);

	nsoption_init(NULL, &nsoptions, &nsoptions_default);

	messages = filepath_find(NULL, "Messages");
	if (messages != NULL) {
		messages_add_from_file(messages);
		free(messages);
	}

	ret = netsurf_init(NULL);
	if (ret != NSERROR_OK) {
		die("NetSurf failed to initialise");
	}

{
		struct browser_window *bw = NULL;
		nsurl *url = NULL;
		nsurl_create("about:blank", &url);
		if (url != NULL) {
			browser_window_create(BW_CREATE_HISTORY, url, NULL, NULL, &bw);
			nsurl_unref(url);
		}
	}

	ntux_run();

	ntux_kill_browser_windows();
	netsurf_exit();
	nsoption_finalise(nsoptions, nsoptions_default);
	nslog_finalise();
}