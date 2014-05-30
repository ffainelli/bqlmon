/*
 * BQLmon
 *
 * Copyright (C) 2014, Florian Fainelli <f.fainelli@gmail.com>
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>

#include "bqlmon.h"

/* Locate the network interface and count its number of transmit queues */
static int bql_sysfs_init(struct bql_ctx *ctx)
{
	struct dirent **namelist;
	char path[PATH_MAX];
	struct utsname uts;
	int maj, min, rel;
	int n;

	n = uname(&uts);
	if (n < 0)
		goto try_file;

	if (strcmp(uts.sysname, "Linux")) {
		fprintf(stderr, "Unsupported OS: %s\n", uts.sysname);
		return -1;
	}

	n = sscanf(uts.release, "%d.%d.%d", &maj, &min, &rel);
	if (n < 3)
		return -1;

	if (maj < 3 && min < 3) {
		fprintf(stderr, "Kernel too old, requires 3.3 for BQL\n");
		return -1;
	}

try_file:
	n = snprintf(path, sizeof(path), "/sys/class/net/%s/queues/",
			ctx->iface);
	if (n <= 0)
		return -1;

	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0) {
		perror("scandir");
		return n;
	}

	while (n--) {
		if (!strncmp(namelist[n]->d_name, "tx-", 3))
			ctx->num_queues++;
		free(namelist[n]);
	}
	free(namelist);

	if (ctx->num_queues == 0) {
		fprintf(stderr, "Kernel too old, or invalid network device\n");
		return -1;
	}
	ctx->num_visible_queues = ctx->num_queues;

	return 0;
}

static int bql_sysfs_file_init(struct bql_sysfs_attr *s, const char *dir,
		const char *name)
{
	size_t n;

	n = snprintf(s->path, sizeof(s->path), "%s/%s", dir, name);
	if (n <= 0)
		return -1;

	return 0;
}

static int bql_sysfs_file_read(struct bql_sysfs_attr *s)
{
	ssize_t n;

	s->fd = open(s->path, O_RDONLY);
	if (s->fd < 0) {
		perror("open");
		return s->fd;
	}

	n = read(s->fd, s->s_val, sizeof(s->s_val) - 1);
	if (n < 0)
		perror("read");
	close(s->fd);

	n = sscanf(s->s_val, "%d", &s->value);
	if (n < 1)
		return -1;

	/* scale the value by 1024 */
	s->value /= 1024;

	return 0;
}

#define ARRAY_SIZE(x)	(sizeof((x)) / sizeof((x)[0]))

static int bql_queue_init(struct bql_q_ctx *q)
{
	char path[PATH_MAX];
	size_t n;
	static const char *attr_names[] = {
		"hold_time", "inflight", "limit", "limit_max", "limit_min" };
	unsigned int i;
	int ret;

	n = snprintf(path, sizeof(path),
			"/sys/class/net/%s/queues/tx-%d/byte_queue_limits",
			q->g->iface, q->queue_num);
	if (n <= 0)
		return -1;

	for (i = 0; i < ARRAY_SIZE(attr_names); i++) {
		ret = bql_sysfs_file_init(&q->attrs[i], path, attr_names[i]);
		if (ret)
			return ret;

		bql_sysfs_file_read(&q->attrs[i]);
	}

	return 0;
}

static inline unsigned int bql_poll_one_queue(struct bql_q_ctx *ctx)
{
	bql_sysfs_file_read(&ctx->attrs[INFLIGHT]);
	bql_sysfs_file_read(&ctx->attrs[LIMIT]);

	return ctx->attrs[INFLIGHT].value;
}

static int bql_queues_create(struct bql_ctx *ctx)
{
	struct bql_q_ctx *tctx;
	unsigned int i;
	size_t size;
	int ret;

	size = sizeof(struct bql_q_ctx) * ctx->num_queues;

	ctx->queues = malloc(size);
	if (!ctx->queues)
		return -ENOMEM;

	memset(ctx->queues, 0, size);

	for (i = 0; i < ctx->num_queues; i++) {
		tctx = &ctx->queues[i];
		tctx->queue_num = i;
		tctx->g = ctx;

		ret = bql_queue_init(tctx);
		if (ret) {
			fprintf(stderr, "failed to initialize queue %d\n", i);
			return ret;
		}
	}

	return ret;
}

static int get_color_thresh(unsigned int val, unsigned int limit)
{
	if (val <= limit / 3)
		return 2;
	else if (val <= (limit * 2) / 3)
		return 3;
	else if (val <= limit)
		return 1;
	else
		return 6;
}

static void bql_draw_arrows(struct bql_ctx *ctx, unsigned int q,
				unsigned int limit)
{
	unsigned int i;
	chtype ch;
	int y, x;

	x = q * QUEUE_SPACING + QUEUE_VAL_X - ctx->x_start;
	y = ctx->rows - QUEUE_VAL_Y - limit - QUEUE_ARROW_Y;

	if (q == ctx->vq_start && q != 0) {
		for (i = 0; i < 3; i++) {
			wmove(ctx->w, y, x + i);
			if (i == 0)
				ch = ACS_LARROW;
			else
				ch = ACS_HLINE;
			waddch(ctx->w, ch);
		}
	}

	if (q == ctx->vq_end - 1 && q != ctx->num_queues - 1) {
		for (i = 3; i-- > 0; ) {
			wmove(ctx->w, y, x - i);
			if (i == 0)
				ch = ACS_RARROW;
			else
				ch = ACS_HLINE;
			waddch(ctx->w, ch);
		}
	}
}

static void bql_draw_one(struct bql_ctx *ctx, unsigned int q)
{
	struct bql_q_ctx *qctx = &ctx->queues[q];
	unsigned int val, limit;
	unsigned int i;
	int color, rows;
	char buf[128];
	int x;

	rows = ctx->rows;
	val = bql_poll_one_queue(qctx);
	limit = ctx->queues[q].attrs[LIMIT].value;

	x = q * QUEUE_SPACING + QUEUE_VAL_X - ctx->x_start;

	snprintf(buf, sizeof(buf), "%02u", q);

	/* Draw the queue number */
	wmove(ctx->w, rows - QUEUE_NUM_Y, q * QUEUE_SPACING + QUEUE_SEP_X - ctx->x_start);
	wattron(ctx->w, COLOR_PAIR(5));
	wattron(ctx->w, A_BOLD);
	waddstr(ctx->w, buf);
	wattroff(ctx->w, A_BOLD);
	wattroff(ctx->w, COLOR_PAIR(5));

	/* Draw the queue value as a histogram */
	for (i = 0; i < val; i++) {
		color = get_color_thresh(i, limit);
		wmove(ctx->w, rows - QUEUE_VAL_Y - i, x);
		wattron(ctx->w, COLOR_PAIR(color));
		waddch(ctx->w, QUEUE_CHAR | A_BOLD);
		wattroff(ctx->w, COLOR_PAIR(color));
	}

	/* Display the queue limit value */
	wmove(ctx->w, rows - QUEUE_VAL_Y - limit, x);
	waddch(ctx->w, ACS_BLOCK);

	/* Display the arrows to indicate there is something */
	bql_draw_arrows(ctx, q, limit);
}

static void bql_draw_main_items(struct bql_ctx *ctx)
{
	unsigned int i;
	int y = PARAMS_Y;

	box(ctx->w, 0, 0);

	wmove(ctx->w, y, PARAMS_X);
	waddstr(ctx->w, "Interface: ");
	wattron(ctx->w, A_BOLD);
	waddstr(ctx->w, ctx->iface);
	wattroff(ctx->w, A_BOLD);

	wmove(ctx->w, ++y, PARAMS_X);
	waddstr(ctx->w, "Frequency: ");
	wattron(ctx->w, A_BOLD);
	wprintw(ctx->w, "%d (msecs)", ctx->poll_freq);
	wattroff(ctx->w, A_BOLD);

	/* Draw the separation line between queue number and values */
	wmove(ctx->w, ++y, PARAMS_X);
	for (i = 0; i < ctx->h_line_val; i++) {
		wmove(ctx->w, ctx->rows - QUEUE_SEP_Y, i + QUEUE_SEP_X);
		waddch(ctx->w, ACS_HLINE);
	}

	y = PARAMS_Y;
	wmove(ctx->w, y, ctx->version_x_pos);
	wattron(ctx->w, A_BOLD);
	waddstr(ctx->w, "BQLmon");
	wattroff(ctx->w, A_BOLD);
	wmove(ctx->w, ++y, ctx->version_x_pos);
	waddstr(ctx->w, "Version: ");
	wattron(ctx->w, A_BOLD);
	wprintw(ctx->w, "%s", VERSION);
	wattroff(ctx->w, A_BOLD);
	wmove(ctx->w, ++y, ctx->version_x_pos);
	wattron(ctx->w, A_BOLD);
	waddstr(ctx->w, "F1 to exit");
	wattroff(ctx->w, A_BOLD);
}

static void bql_recalc_visible_queues(struct bql_ctx *ctx)
{
	unsigned int val;

	/* Get the number of times we need to repeat the ACS_HLINE */
	val = ctx->num_queues * QUEUE_SPACING - 1;
	if (val >= ctx->cols) {
		val = ctx->cols - 2 * QUEUE_SEP_Y;
		ctx->num_visible_queues = (ctx->cols / QUEUE_SPACING) - 1;
	}
	ctx->h_line_val = val;
	ctx->x_end = val + 1;

	ctx->vq_start = ctx->x_start / QUEUE_SPACING;
	ctx->vq_end = ctx->num_visible_queues + ctx->vq_start;
	if (ctx->vq_end >= ctx->num_queues)
		ctx->vq_end = ctx->num_queues;
}

static void bql_draw_loop(struct bql_ctx *ctx)
{
	unsigned int q, exit = 0;
	int ch;

	while (!exit) {
		wclear(ctx->w);

		bql_draw_main_items(ctx);

		for (q = ctx->vq_start; q < ctx->vq_end; q++)
			bql_draw_one(ctx, q);

		ch = wgetch(ctx->w);
		switch (ch) {
		case KEY_F(1):
			exit = 1;
			break;

		case KEY_LEFT:
			if (ctx->x_start >= QUEUE_SPACING)
				ctx->x_start -= QUEUE_SPACING;
			break;

		case KEY_RIGHT:
			if (ctx->x_end - ctx->x_start >= QUEUE_SPACING &&
				ctx->vq_end < ctx->num_queues)
				ctx->x_start += QUEUE_SPACING;
			break;
		default:
			usleep(ctx->poll_freq * 1000);
			break;
		}

		bql_recalc_visible_queues(ctx);
		wrefresh(ctx->w);
	}
}

static int bql_init_term(struct bql_ctx *ctx)
{
	int rows, cols;

	initscr();
	getmaxyx(stdscr, rows, cols);
	cbreak();
	noecho();
	curs_set(0);

	ctx->rows = rows;
	ctx->cols = cols;
	ctx->x_start = 0;

	ctx->w = newwin(rows, cols, 0, 0);
	if (!ctx->w) {
		fprintf(stderr, "failed to create window\n");
		return 1;
	}

	keypad(ctx->w, TRUE);
	nodelay(ctx->w, TRUE);

	if (!has_colors()) {
		fprintf(stderr, "terminal does not supporte colors!\n");
		endwin();
		return 1;
	}

	start_color();

	init_pair(1, COLOR_RED, COLOR_BLACK);
	init_pair(2, COLOR_GREEN, COLOR_BLACK);
	init_pair(3, COLOR_YELLOW, COLOR_BLACK);
	init_pair(4, COLOR_WHITE, COLOR_BLACK);
	init_pair(5, COLOR_WHITE, COLOR_BLUE);
	init_pair(6, COLOR_MAGENTA, COLOR_BLACK);

	bql_recalc_visible_queues(ctx);

	ctx->version_x_pos = ctx->cols - strlen("Version: ") -
		strlen(VERSION) - QUEUE_SPACING;

	return 0;
}

static void usage(const char *pname)
{
	fprintf(stderr, "Usage: %s [options]\n"
		"-i:	interface\n"
		"-f:	poll frequency (msecs)\n"
		"-h:	this help\n", pname);
}

int main(int argc, char **argv)
{
	struct bql_ctx *ctx;
	int opt, ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	memset(ctx, 0, sizeof(*ctx));

	while ((opt = getopt(argc, argv, "i:f:h")) > 0) {
		switch (opt) {
		case 'i':
			ctx->iface = optarg;
			break;
		case 'f':
			ctx->poll_freq = strtoul(optarg, 0, 10);
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (!ctx->iface)
		ctx->iface = "eth0";

	if (!ctx->poll_freq)
		ctx->poll_freq = 10;

	ret = bql_sysfs_init(ctx);
	if (ret)
		goto out;

	ret = bql_queues_create(ctx);
	if (ret)
		goto out;

	ret = bql_init_term(ctx);
	if (ret)
		goto out_win;

	bql_draw_loop(ctx);

out_win:
	endwin();
out:
	free(ctx);
	return 0;
}
