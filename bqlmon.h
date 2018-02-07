#ifndef __BQLMON_H
#define __BQLMON_H

#include <limits.h>
#include <curses.h>

#include <linux/ethtool.h>

/*
 * 1:1 copy of the sysfs attributes
 */
struct bql_sysfs_attr {
	int fd;
	char s_val[512];
	unsigned int value;
};

enum bql_sysfs_attr_map {
	HOLD_TIME = 0,
	INFLIGHT,
	LIMIT,
	LIMIT_MAX,
	LIMIT_MIN,
};

struct bql_q_ctx {
	unsigned int queue_num;
	struct bql_sysfs_attr attrs[5];
	struct bql_ctx *g;
};

struct bql_ctx {
	char *iface;
	char *filename;
	FILE *fd;
	struct ethtool_drvinfo info;
	unsigned int poll_freq;
	int timer;
	int count;
	int monitor;
	int scaling;
	unsigned int num_queues;
	unsigned int vq_start;
	unsigned int vq_end;
	struct bql_q_ctx *queues;
	WINDOW *w;
	int rows;
	int cols;
	unsigned int x_start;
	unsigned int h_line_val;
	unsigned int version_x_pos;
};

/* Definitions for the graphical representation */
#define QUEUE_SPACING	3	/* px */
#define QUEUE_CHAR	ACS_CKBOARD

#define QUEUE_VAL_Y	4
#define QUEUE_VAL_X	3

#define QUEUE_SEP_Y	3
#define QUEUE_SEP_X	2

#define QUEUE_NUM_Y	2
#define QUEUE_ARROW_Y	4

#define PARAMS_X	3
#define PARAMS_Y	2

#define VERSION		"0.1"

#endif /* __BQLMON_H */
