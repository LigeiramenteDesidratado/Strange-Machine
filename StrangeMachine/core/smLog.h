#ifndef SM_LOG_H
#define SM_LOG_H

#include "core/smBase.h"

#include "core/smCore.h"
#include "core/smString.h"

b8 log_init(void);
void log_teardown(void);

#define LOG_INFO 0
#define LOG_WARN 1
#define LOG_ERRO 2
#define LOG_TRAC 3
#define LOG_DEBU 4

void log__log(u32 level, str8 file, u32 line, str8 format, ...);

#define LOG_FILE (str8_from(sm__file_name))

#define log_info(...)  log__log(LOG_INFO, LOG_FILE, sm__file_line, __VA_ARGS__)
#define log_warn(...)  log__log(LOG_WARN, LOG_FILE, sm__file_line, __VA_ARGS__)
#define log_error(...) log__log(LOG_ERRO, LOG_FILE, sm__file_line, __VA_ARGS__)
#define log_trace(...) log__log(LOG_TRAC, LOG_FILE, sm__file_line, __VA_ARGS__)
#define log_debug(...) log__log(LOG_DEBU, LOG_FILE, sm__file_line, __VA_ARGS__)

// void log_info(struct arena *arena, str8 message, ...);
#endif //
