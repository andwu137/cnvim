#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#if _WIN32
  #include <windows.h>
  #define PATH_MAX MAX_PATH
#endif

#ifndef uint
typedef unsigned int uint;
#endif

#define STRINGIFY_IND(x) #x
#define STRINGIFY(x) STRINGIFY_IND(x)
#define MULTILINE_STRING(x) STRINGIFY(x)

#if DEBUG
#define FILE_POS "(" STRINGIFY(__FILE__) ":" STRINGIFY(__LINE__) "): "
#else
#define FILE_POS
#endif

#define PANIC_FMT(L, fmt, ...) luaL_error(L, fmt FILE_POS, __VA_ARGS__)
#define PANIC(L, msg) luaL_error(L, "ERROR: " FILE_POS msg)
#if DEBUG
#define ASSERT(L, b) do { if(!(b)) { PANIC(L, "assert(" STRINGIFY(b) ")"); } } while(0)
#else
#define ASSERT(L, b) (void)(b)
#endif

#define STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#define PERF_TIME_LIST \
  PERF_TIME_X(Total) \
  PERF_TIME_X(Path) \
  PERF_TIME_X(Opt) \
  PERF_TIME_X(Download)

enum Perf_Time : int
{
#define PERF_TIME_X(n) Perf_Time_##n,
  PERF_TIME_LIST
#undef PERF_TIME_X
  Perf_Time_Count,
};

#define START_PERF_TIME(g, n) clock_gettime(CLOCK_MONOTONIC, &((g)[n][0]))
#define END_PERF_TIME(g, n) clock_gettime(CLOCK_MONOTONIC, &((g)[n][1]))
#define SAME_PERF_TIME(g, n, x) (g)[n][1] = (g)[x][0]

#endif // CONFIG_H
