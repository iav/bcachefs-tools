
#include <stdio.h>
#include <linux/printbuf.h>

void prt_vprintf(struct printbuf *out, const char *fmt, va_list args)
{
	int len;

	do {
		va_list args2;

		va_copy(args2, args);
		len = vsnprintf(out->buf + out->pos, printbuf_remaining(out), fmt, args2);
	} while (len + 1 >= printbuf_remaining(out) &&
		 !printbuf_make_room(out, len + 1));

	len = min_t(size_t, len,
		  printbuf_remaining(out) ? printbuf_remaining(out) - 1 : 0);
	out->pos += len;
}

void prt_printf(struct printbuf *out, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	prt_vprintf(out, fmt, args);
	va_end(args);
}

void prt_u64(struct printbuf *out, u64 v)
{
	prt_printf(out, "%llu", v);
}
