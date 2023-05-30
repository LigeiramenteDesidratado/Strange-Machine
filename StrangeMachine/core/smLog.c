#include "core/smBase.h"

#include "core/smLog.h"
#include "core/smString.h"

struct log
{
	struct arena arena;
};

static struct log LC; // log context

b8
log_init(struct buf base_memory)
{
	arena_make(&LC.arena, base_memory);

	return (true);
}

void
log_teardown(void)
{
	arena_release(&LC.arena);
}

static str8 sign[] = {str8_from("I"), str8_from("W"), str8_from("E"), str8_from("T"), str8_from("D")};
static str8 sign_color[] = {
    str8_from("\x1b[32m"), str8_from("\x1b[33m"), str8_from("\x1b[31m"), str8_from("\x1b[94m"), str8_from("\x1b[36m")};

void
log__log(u32 level, str8 file, u32 line, str8 format, ...)
{
	static i8 buf[16];

	// TODO: time format
	time_t t = time(0);
	struct tm *timer = localtime(&t);

	u32 len = (u32)strftime(buf, sizeof(buf), "%H:%M:%S", timer);
	str8 htime = {.idata = buf, .size = len};

	str8_printf(&LC.arena, str8_from("{s} {s}[{s}]\x1b[0m \x1b[90m{s}:{u3d}:\x1b[0m "), htime, sign_color[level],
	    sign[level], file, line);

	va_list args;
	va_start(args, format);

	str8_vprintf(&LC.arena, format, args);
	str8_print(str8_newline_const);

	va_end(args);
}
