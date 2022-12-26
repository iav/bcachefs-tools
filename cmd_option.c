/*
 * Authors: Kent Overstreet <kent.overstreet@gmail.com>
 *
 * GPLv2
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <uuid/uuid.h>

#include "cmds.h"
#include "libbcachefs.h"
#include "libbcachefs/errcode.h"
#include "libbcachefs/opts.h"
#include "libbcachefs/super-io.h"

static void set_option_usage(void)
{
	puts("bcachefs set-option \n"
	     "Usage: bcachefs set-option [OPTION].. device\n"
	     "\n"
	     "Options:\n");
	bch2_opts_usage(OPT_MOUNT);
	puts("  -h, --help                  display this help and exit\n"
	     "Report bugs to <linux-bcachefs@vger.kernel.org>");
	exit(EXIT_SUCCESS);
}

int cmd_set_option(int argc, char *argv[])
{
	struct bch_opt_strs new_opt_strs = bch2_cmdline_opts_get(&argc, argv, OPT_MOUNT);
	struct bch_opts new_opts = bch2_parse_opts(new_opt_strs);
	struct bch_opts open_opts = bch2_opts_empty();
	unsigned i;
	int opt, ret = 0;

	opt_set(open_opts, nostart, true);

	while ((opt = getopt(argc, argv, "h")) != -1)
		switch (opt) {
		case 'h':
			set_option_usage();
			break;
		}
	args_shift(optind);

	if (!argc) {
		fprintf(stderr, "Please supply device(s)\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < argc; i++)
		if (dev_mounted(argv[i]))
			goto online;

	struct bch_fs *c = bch2_fs_open(argv, argc, open_opts);
	if (IS_ERR(c)) {
		fprintf(stderr, "error opening %s: %s\n", argv[0], bch2_err_str(PTR_ERR(c)));
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < bch2_opts_nr; i++) {
		u64 v = bch2_opt_get_by_id(&new_opts, i);

		if (!bch2_opt_defined_by_id(&new_opts, i))
			continue;

		ret = bch2_opt_check_may_set(c, i, v);
		if (ret < 0) {
			fprintf(stderr, "error setting %s: %i\n",
				bch2_opt_table[i].attr.name, ret);
			break;
		}

		bch2_opt_set_sb(c, bch2_opt_table + i, v);
		bch2_opt_set_by_id(&c->opts, i, v);
	}

	bch2_fs_stop(c);
	return ret;
online:
	{
		unsigned dev_idx;
		struct bchfs_handle fs = bchu_fs_open_by_dev(argv[i], &dev_idx);

		for (i = 0; i < bch2_opts_nr; i++) {
			if (!new_opt_strs.by_id[i])
				continue;

			char *path = mprintf("options/%s", bch2_opt_table[i].attr.name);

			write_file_str(fs.sysfs_fd, path, new_opt_strs.by_id[i]);
			free(path);
		}
	}
	return 0;
}
