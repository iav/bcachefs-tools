
#include <stdio.h>

#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/shrinker.h>

#include "tools-util.h"

static LIST_HEAD(shrinker_list);
static DEFINE_MUTEX(shrinker_lock);

int register_shrinker(struct shrinker *shrinker, const char *fmt, ...)
{
	mutex_lock(&shrinker_lock);
	list_add_tail(&shrinker->list, &shrinker_list);
	mutex_unlock(&shrinker_lock);
	return 0;
}

void unregister_shrinker(struct shrinker *shrinker)
{
	mutex_lock(&shrinker_lock);
	list_del(&shrinker->list);
	mutex_unlock(&shrinker_lock);
}

struct meminfo {
	u64		total;
	u64		available;
};

static u64 parse_meminfo_line(const char *line)
{
	u64 v;

	if (sscanf(line, " %llu kB", &v) < 1)
		die("sscanf error");
	return v << 10;
}

void si_meminfo(struct sysinfo *val)
{
	size_t len, n = 0;
	char *line = NULL;
	const char *v;
	FILE *f;

	memset(val, 0, sizeof(*val));
	val->mem_unit = 1;

	f = fopen("/proc/meminfo", "r");
	if (!f)
		return;

	while ((len = getline(&line, &n, f)) != -1) {
		if ((v = strcmp_prefix(line, "MemTotal:")))
			val->totalram = parse_meminfo_line(v);

		if ((v = strcmp_prefix(line, "MemAvailable:")))
			val->freeram = parse_meminfo_line(v);
	}

	fclose(f);
	free(line);
}

static void run_shrinkers_allocation_failed(gfp_t gfp_mask)
{
	struct shrinker *shrinker;

	mutex_lock(&shrinker_lock);
	list_for_each_entry(shrinker, &shrinker_list, list) {
		struct shrink_control sc = { .gfp_mask	= gfp_mask, };

		unsigned long have = shrinker->count_objects(shrinker, &sc);

		sc.nr_to_scan = have / 8;

		shrinker->scan_objects(shrinker, &sc);
	}
	mutex_unlock(&shrinker_lock);
}

void run_shrinkers(gfp_t gfp_mask, bool allocation_failed)
{
	struct shrinker *shrinker;
	struct sysinfo info;
	s64 want_shrink;

	if (!(gfp_mask & GFP_KERNEL))
		return;

	/* Fast out if there are no shrinkers to run. */
	if (list_empty(&shrinker_list))
		return;

	if (allocation_failed) {
		run_shrinkers_allocation_failed(gfp_mask);
		return;
	}

	si_meminfo(&info);

	if (info.totalram && info.freeram) {
		want_shrink = (info.totalram >> 2) - info.freeram;

		if (want_shrink <= 0)
			return;
	} else {
		/* If we weren't able to read /proc/meminfo, we must be pretty
		 * low: */

		want_shrink = 8 << 20;
	}

	mutex_lock(&shrinker_lock);
	list_for_each_entry(shrinker, &shrinker_list, list) {
		struct shrink_control sc = {
			.gfp_mask	= gfp_mask,
			.nr_to_scan	= want_shrink >> PAGE_SHIFT
		};

		shrinker->scan_objects(shrinker, &sc);
	}
	mutex_unlock(&shrinker_lock);
}

static int shrinker_thread(void *arg)
{
	while (!kthread_should_stop()) {
		sleep(1);
		run_shrinkers(GFP_KERNEL, false);
	}

	return 0;
}

struct task_struct *shrinker_task;

__attribute__((constructor(103)))
static void shrinker_thread_init(void)
{
	shrinker_task = kthread_run(shrinker_thread, NULL, "shrinkers");
	BUG_ON(IS_ERR(shrinker_task));
}

__attribute__((destructor(103)))
static void shrinker_thread_exit(void)
{
	int ret = kthread_stop(shrinker_task);
	BUG_ON(ret);

	shrinker_task = NULL;
}
