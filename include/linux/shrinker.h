#ifndef __TOOLS_LINUX_SHRINKER_H
#define __TOOLS_LINUX_SHRINKER_H

#include <linux/list.h>
#include <linux/types.h>

struct shrink_control {
	gfp_t gfp_mask;
	unsigned long nr_to_scan;
};

#define SHRINK_STOP (~0UL)

struct printbuf;
struct shrinker {
	unsigned long (*count_objects)(struct shrinker *,
				       struct shrink_control *sc);
	unsigned long (*scan_objects)(struct shrinker *,
				      struct shrink_control *sc);
	void (*to_text)(struct printbuf *, struct shrinker *);

	int seeks;	/* seeks to recreate an obj */
	long batch;	/* reclaim batch size, 0 = default */
	struct list_head list;
};

int register_shrinker(struct shrinker *, const char *, ...);
void unregister_shrinker(struct shrinker *);

void run_shrinkers(gfp_t gfp_mask, bool);

#endif /* __TOOLS_LINUX_SHRINKER_H */
