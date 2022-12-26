#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cmds.h"
#include "libbcachefs.h"
#include "qcow2.h"
#include "tools-util.h"

#include "libbcachefs/bcachefs.h"
#include "libbcachefs/btree_cache.h"
#include "libbcachefs/btree_io.h"
#include "libbcachefs/btree_iter.h"
#include "libbcachefs/checksum.h"
#include "libbcachefs/error.h"
#include "libbcachefs/extents.h"
#include "libbcachefs/super.h"

static void list_keys(struct bch_fs *c, enum btree_id btree_id,
		      struct bpos start, struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, btree_id, start,
			   BTREE_ITER_ALL_SNAPSHOTS|
			   BTREE_ITER_PREFETCH, k, ret) {
		if (bkey_cmp(k.k->p, end) > 0)
			break;

		printbuf_reset(&buf);
		bch2_bkey_val_to_text(&buf, c, k);
		puts(buf.buf);
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	printbuf_exit(&buf);
}

static void list_btree_formats(struct bch_fs *c, enum btree_id btree_id, unsigned level,
			       struct bpos start, struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct btree *b;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	__for_each_btree_node(&trans, iter, btree_id, start, 0, level, 0, b, ret) {
		if (bkey_cmp(b->key.k.p, end) > 0)
			break;

		printbuf_reset(&buf);
		bch2_btree_node_to_text(&buf, c, b);
		puts(buf.buf);
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret)
		die("error %s walking btree nodes", bch2_err_str(ret));

	bch2_trans_exit(&trans);
	printbuf_exit(&buf);
}

static void list_nodes(struct bch_fs *c, enum btree_id btree_id, unsigned level,
		       struct bpos start, struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct btree *b;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	__for_each_btree_node(&trans, iter, btree_id, start, 0, level, 0, b, ret) {
		if (bkey_cmp(b->key.k.p, end) > 0)
			break;

		printbuf_reset(&buf);
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));
		fputs(buf.buf, stdout);
		putchar('\n');
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret)
		die("error %s walking btree nodes", bch2_err_str(ret));

	bch2_trans_exit(&trans);
	printbuf_exit(&buf);
}

static void print_node_ondisk(struct bch_fs *c, struct btree *b)
{
	struct btree_node *n_ondisk;
	struct extent_ptr_decoded pick;
	struct bch_dev *ca;
	struct bio *bio;
	unsigned offset = 0;
	int ret;

	if (bch2_bkey_pick_read_device(c, bkey_i_to_s_c(&b->key), NULL, &pick) <= 0) {
		printf("error getting device to read from\n");
		return;
	}

	ca = bch_dev_bkey_exists(c, pick.ptr.dev);
	if (!bch2_dev_get_ioref(ca, READ)) {
		printf("error getting device to read from\n");
		return;
	}

	n_ondisk = aligned_alloc(block_bytes(c), btree_bytes(c));

	bio = bio_alloc_bioset(ca->disk_sb.bdev,
			       buf_pages(n_ondisk, btree_bytes(c)),
			       REQ_OP_READ|REQ_META,
			       GFP_NOIO,
			       &c->btree_bio);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bch2_bio_map(bio, n_ondisk, btree_bytes(c));

	ret = submit_bio_wait(bio);
	if (ret)
		die("error reading btree node: %i", ret);

	bio_put(bio);
	percpu_ref_put(&ca->io_ref);

	while (offset < btree_sectors(c)) {
		struct bset *i;
		struct nonce nonce;
		struct bch_csum csum;
		struct bkey_packed *k;
		unsigned sectors;

		if (!offset) {
			i = &n_ondisk->keys;

			if (!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i)))
				die("unknown checksum type at offset %u: %llu",
				    offset, BSET_CSUM_TYPE(i));

			nonce = btree_nonce(i, offset << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, n_ondisk);

			if (bch2_crc_cmp(csum, n_ondisk->csum))
				die("invalid checksum\n");

			bset_encrypt(c, i, offset << 9);

			sectors = vstruct_sectors(n_ondisk, c->block_bits);
		} else {
			struct btree_node_entry *bne = (void *) n_ondisk + (offset << 9);

			i = &bne->keys;

			if (i->seq != n_ondisk->keys.seq)
				break;

			if (!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i)))
				die("unknown checksum type at offset %u: %llu",
				    offset, BSET_CSUM_TYPE(i));

			nonce = btree_nonce(i, offset << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);

			if (bch2_crc_cmp(csum, bne->csum))
				die("invalid checksum");

			bset_encrypt(c, i, offset << 9);

			sectors = vstruct_sectors(bne, c->block_bits);
		}

		fprintf(stdout, "  offset %u version %u, journal seq %llu\n",
			offset,
			le16_to_cpu(i->version),
			le64_to_cpu(i->journal_seq));
		offset += sectors;

		for (k = i->start; k != vstruct_last(i); k = bkey_next(k)) {
			struct bkey u;
			struct printbuf buf = PRINTBUF;

			printbuf_indent_add(&buf, 4);

			bch2_bkey_val_to_text(&buf, c, bkey_disassemble(b, k, &u));
			fprintf(stdout, "%s\n", buf.buf);

			printbuf_exit(&buf);
		}
	}

	free(n_ondisk);
}

static void list_nodes_ondisk(struct bch_fs *c, enum btree_id btree_id, unsigned level,
			      struct bpos start, struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct btree *b;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	__for_each_btree_node(&trans, iter, btree_id, start, 0, level, 0, b, ret) {
		if (bkey_cmp(b->key.k.p, end) > 0)
			break;

		printbuf_reset(&buf);
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));
		fputs(buf.buf, stdout);
		putchar('\n');

		print_node_ondisk(c, b);
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret)
		die("error %s walking btree nodes", bch2_err_str(ret));

	bch2_trans_exit(&trans);
	printbuf_exit(&buf);
}

static void list_nodes_keys(struct bch_fs *c, enum btree_id btree_id, unsigned level,
			    struct bpos start, struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct btree_node_iter node_iter;
	struct bkey unpacked;
	struct bkey_s_c k;
	struct btree *b;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	__for_each_btree_node(&trans, iter, btree_id, start, 0, level, 0, b, ret) {
		if (bkey_cmp(b->key.k.p, end) > 0)
			break;

		printbuf_reset(&buf);
		bch2_btree_node_to_text(&buf, c, b);
		fputs(buf.buf, stdout);

		for_each_btree_node_key_unpack(b, k, &node_iter, &unpacked) {
			printbuf_reset(&buf);
			bch2_bkey_val_to_text(&buf, c, k);
			putchar('\t');
			puts(buf.buf);
		}
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret)
		die("error %s walking btree nodes", bch2_err_str(ret));

	bch2_trans_exit(&trans);
	printbuf_exit(&buf);
}

static void list_keys_usage(void)
{
	puts("bcachefs list - list filesystem metadata to stdout\n"
	     "Usage: bcachefs list [OPTION]... <devices>\n"
	     "\n"
	     "Options:\n"
	     "  -b (extents|inodes|dirents|xattrs)    Btree to list from\n"
	     "  -l level                              Btree depth to descend to (0 == leaves)\n"
	     "  -s inode:offset                       Start position to list from\n"
	     "  -e inode:offset                       End position\n"
	     "  -i inode                              List keys for a given inode number\n"
	     "  -m (keys|formats|nodes|nodes_ondisk|nodes_keys)\n"
	     "                                        List mode\n"
	     "  -f                                    Check (fsck) the filesystem first\n"
	     "  -v                                    Verbose mode\n"
	     "  -h                                    Display this help and exit\n"
	     "Report bugs to <linux-bcachefs@vger.kernel.org>");
}

#define LIST_MODES()		\
	x(keys)			\
	x(formats)		\
	x(nodes)		\
	x(nodes_ondisk)		\
	x(nodes_keys)

enum list_modes {
#define x(n)	LIST_MODE_##n,
	LIST_MODES()
#undef x
};

static const char * const list_modes[] = {
#define x(n)	#n,
	LIST_MODES()
#undef x
	NULL
};

int cmd_list(int argc, char *argv[])
{
	struct bch_opts opts = bch2_opts_empty();
	enum btree_id btree_id_start	= 0;
	enum btree_id btree_id_end	= BTREE_ID_NR;
	enum btree_id btree_id;
	unsigned level = 0;
	struct bpos start = POS_MIN, end = POS_MAX;
	u64 inum = 0;
	int mode = 0, opt;

	opt_set(opts, nochanges,	true);
	opt_set(opts, norecovery,	true);
	opt_set(opts, degraded,		true);
	opt_set(opts, errors,		BCH_ON_ERROR_continue);

	while ((opt = getopt(argc, argv, "b:l:s:e:i:m:fvh")) != -1)
		switch (opt) {
		case 'b':
			btree_id_start = read_string_list_or_die(optarg,
						bch2_btree_ids, "btree id");
			btree_id_end = btree_id_start + 1;
			break;
		case 'l':
			if (kstrtouint(optarg, 10, &level) || level >= BTREE_MAX_DEPTH)
				die("invalid level");
			break;
		case 's':
			start	= bpos_parse(optarg);
			break;
		case 'e':
			end	= bpos_parse(optarg);
			break;
		case 'i':
			if (kstrtoull(optarg, 10, &inum))
				die("invalid inode %s", optarg);
			start	= POS(inum, 0);
			end	= POS(inum + 1, 0);
			break;
		case 'm':
			mode = read_string_list_or_die(optarg,
						list_modes, "list mode");
			break;
		case 'f':
			opt_set(opts, fix_errors, FSCK_OPT_YES);
			opt_set(opts, norecovery, false);
			break;
		case 'v':
			opt_set(opts, verbose, true);
			break;
		case 'h':
			list_keys_usage();
			exit(EXIT_SUCCESS);
		}
	args_shift(optind);

	if (!argc)
		die("Please supply device(s)");

	struct bch_fs *c = bch2_fs_open(argv, argc, opts);
	if (IS_ERR(c))
		die("error opening %s: %s", argv[0], bch2_err_str(PTR_ERR(c)));


	for (btree_id = btree_id_start;
	     btree_id < btree_id_end;
	     btree_id++) {
		switch (mode) {
		case LIST_MODE_keys:
			list_keys(c, btree_id, start, end);
			break;
		case LIST_MODE_formats:
			list_btree_formats(c, btree_id, level, start, end);
			break;
		case LIST_MODE_nodes:
			list_nodes(c, btree_id, level, start, end);
			break;
		case LIST_MODE_nodes_ondisk:
			list_nodes_ondisk(c, btree_id, level, start, end);
			break;
		case LIST_MODE_nodes_keys:
			list_nodes_keys(c, btree_id, level, start, end);
			break;
		default:
			die("Invalid mode");
		}
	}

	bch2_fs_stop(c);
	return 0;
}
