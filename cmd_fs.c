
#include <stdio.h>
#include <sys/ioctl.h>

#include <uuid/uuid.h>

#include "linux/sort.h"

#include "libbcachefs/bcachefs_ioctl.h"
#include "libbcachefs/darray.h"
#include "libbcachefs/opts.h"

#include "cmds.h"
#include "libbcachefs.h"

static void __dev_usage_type_to_text(struct printbuf *out,
				     const char *type,
				     unsigned bucket_size,
				     u64 buckets, u64 sectors, u64 frag)
{
	prt_printf(out, "%s:", type);
	prt_tab(out);

	prt_units_u64(out, sectors << 9);
	prt_tab_rjust(out);

	prt_printf(out, "%llu", buckets);
	prt_tab_rjust(out);

	if (frag) {
		prt_units_u64(out, frag << 9);
		prt_tab_rjust(out);
	}
	prt_newline(out);
}

static void dev_usage_type_to_text(struct printbuf *out,
				   struct bch_ioctl_dev_usage *u,
				   enum bch_data_type type)
{
	__dev_usage_type_to_text(out, bch2_data_types[type],
			u->bucket_size,
			u->d[type].buckets,
			u->d[type].sectors,
			u->d[type].fragmented);
}

static void dev_usage_to_text(struct printbuf *out,
			      struct bchfs_handle fs,
			      struct dev_name *d)
{
	struct bch_ioctl_dev_usage u = bchu_dev_usage(fs, d->idx);
	unsigned i;

	prt_newline(out);
	prt_printf(out, "%s (device %u):", d->label ?: "(no label)", d->idx);
	prt_tab(out);
	prt_str(out, d->dev ?: "(device not found)");
	prt_tab_rjust(out);

	prt_str(out, bch2_member_states[u.state]);
	prt_tab_rjust(out);

	prt_newline(out);

	printbuf_indent_add(out, 2);
	prt_tab(out);

	prt_str(out, "data");
	prt_tab_rjust(out);

	prt_str(out, "buckets");
	prt_tab_rjust(out);

	prt_str(out, "fragmented");
	prt_tab_rjust(out);

	prt_newline(out);

	for (i = 0; i < BCH_DATA_NR; i++)
		dev_usage_type_to_text(out, &u, i);
	__dev_usage_type_to_text(out, "erasure coded",
				 u.bucket_size,
				 u.buckets_ec, u.buckets_ec * u.bucket_size, 0);

	prt_str(out, "capacity:");
	prt_tab(out);

	prt_units_u64(out, (u.nr_buckets * u.bucket_size) << 9);
	prt_tab_rjust(out);
	prt_printf(out, "%llu", u.nr_buckets);
	prt_tab_rjust(out);

	printbuf_indent_sub(out, 2);

	prt_newline(out);
}

static int dev_by_label_cmp(const void *_l, const void *_r)
{
	const struct dev_name *l = _l, *r = _r;

	return  (l->label && r->label
		 ? strcmp(l->label, r->label) : 0) ?:
		(l->dev && r->dev
		 ? strcmp(l->dev, r->dev) : 0) ?:
		cmp_int(l->idx, r->idx);
}

static struct dev_name *dev_idx_to_name(dev_names *dev_names, unsigned idx)
{
	struct dev_name *dev;

	darray_for_each(*dev_names, dev)
		if (dev->idx == idx)
			return dev;

	return NULL;
}

static void replicas_usage_to_text(struct printbuf *out,
				   const struct bch_replicas_usage *r,
				   dev_names *dev_names)
{
	unsigned i;

	if (!r->sectors)
		return;

	char devs[4096], *d = devs;
	*d++ = '[';

	for (i = 0; i < r->r.nr_devs; i++) {
		unsigned dev_idx = r->r.devs[i];
		struct dev_name *dev = dev_idx_to_name(dev_names, dev_idx);

		if (i)
			*d++ = ' ';

		d += dev && dev->dev
			? sprintf(d, "%s", dev->dev)
			: sprintf(d, "%u", dev_idx);
	}
	*d++ = ']';
	*d++ = '\0';

	prt_printf(out, "%s: ", bch2_data_types[r->r.data_type]);
	prt_tab(out);

	prt_printf(out, "%u/%u ", r->r.nr_required, r->r.nr_devs);
	prt_tab(out);

	prt_printf(out, "%s ", devs);
	prt_tab(out);

	prt_units_u64(out, r->sectors << 9);
	prt_tab_rjust(out);
	prt_newline(out);
}

#define for_each_usage_replica(_u, _r)					\
	for (_r = (_u)->replicas;					\
	     _r != (void *) (_u)->replicas + (_u)->replica_entries_bytes;\
	     _r = replicas_usage_next(_r),				\
	     BUG_ON((void *) _r > (void *) (_u)->replicas + (_u)->replica_entries_bytes))

static void fs_usage_to_text(struct printbuf *out, const char *path)
{
	unsigned i;

	struct bchfs_handle fs = bcache_fs_open(path);

	struct dev_name *dev;
	dev_names dev_names = bchu_fs_get_devices(fs);

	struct bch_ioctl_fs_usage *u = bchu_fs_usage(fs);

	prt_str(out, "Filesystem: ");
	pr_uuid(out, fs.uuid.b);
	prt_newline(out);

	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, 20);
	printbuf_tabstop_push(out, 16);

	prt_str(out, "Size:");
	prt_tab(out);
	prt_units_u64(out, u->capacity << 9);
	prt_tab_rjust(out);
	prt_newline(out);

	prt_str(out, "Used:");
	prt_tab(out);
	prt_units_u64(out, u->used << 9);
	prt_tab_rjust(out);
	prt_newline(out);

	prt_str(out, "Online reserved:");
	prt_tab(out);
	prt_units_u64(out, u->online_reserved << 9);
	prt_tab_rjust(out);
	prt_newline(out);

	prt_newline(out);

	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, 16);
	printbuf_tabstop_push(out, 16);
	printbuf_tabstop_push(out, 18);
	printbuf_tabstop_push(out, 18);

	prt_str(out, "Data type");
	prt_tab(out);

	prt_str(out, "Required/total");
	prt_tab(out);

	prt_str(out, "Devices");
	prt_newline(out);

	for (i = 0; i < BCH_REPLICAS_MAX; i++) {
		if (!u->persistent_reserved[i])
			continue;

		prt_str(out, "reserved:");
		prt_tab(out);
		prt_printf(out, "%u/%u ", 1, i);
		prt_tab(out);
		prt_str(out, "[] ");
		prt_units_u64(out, u->persistent_reserved[i] << 9);
		prt_tab_rjust(out);
		prt_newline(out);
	}

	struct bch_replicas_usage *r;

	for_each_usage_replica(u, r)
		if (r->r.data_type < BCH_DATA_user)
			replicas_usage_to_text(out, r, &dev_names);

	for_each_usage_replica(u, r)
		if (r->r.data_type == BCH_DATA_user &&
		    r->r.nr_required <= 1)
			replicas_usage_to_text(out, r, &dev_names);

	for_each_usage_replica(u, r)
		if (r->r.data_type == BCH_DATA_user &&
		    r->r.nr_required > 1)
			replicas_usage_to_text(out, r, &dev_names);

	for_each_usage_replica(u, r)
		if (r->r.data_type > BCH_DATA_user)
			replicas_usage_to_text(out, r, &dev_names);

	free(u);

	sort(dev_names.data, dev_names.nr,
	     sizeof(dev_names.data[0]), dev_by_label_cmp, NULL);

	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, 16);
	printbuf_tabstop_push(out, 20);
	printbuf_tabstop_push(out, 16);
	printbuf_tabstop_push(out, 14);

	darray_for_each(dev_names, dev)
		dev_usage_to_text(out, fs, dev);

	darray_for_each(dev_names, dev) {
		free(dev->dev);
		free(dev->label);
	}
	darray_exit(&dev_names);

	bcache_fs_close(fs);
}

int fs_usage(void)
{
       puts("bcachefs fs - manage a running filesystem\n"
            "Usage: bcachefs fs <CMD> [OPTION]... path\n"
            "\n"
            "Commands:\n"
            "  usage                      show disk usage\n"
            "\n"
            "Report bugs to <linux-bcachefs@vger.kernel.org>");
       return 0;
}

int cmd_fs_usage(int argc, char *argv[])
{
	bool human_readable = false;
	struct printbuf buf = PRINTBUF;
	char *fs;
	int opt;

	while ((opt = getopt(argc, argv, "h")) != -1)
		switch (opt) {
		case 'h':
			human_readable = true;
			break;
		}
	args_shift(optind);

	if (!argc) {
		printbuf_reset(&buf);
		buf.human_readable_units = human_readable;
		fs_usage_to_text(&buf, ".");
		printf("%s", buf.buf);
	} else {
		while ((fs = arg_pop())) {
			printbuf_reset(&buf);
			buf.human_readable_units = human_readable;
			fs_usage_to_text(&buf, fs);
			printf("%s", buf.buf);
		}
	}

	printbuf_exit(&buf);
	return 0;
}
