#ifndef CONFIG_H
#define CONFIG_H

#if _WIN32
  #include <windows.h>
  #define PATH_MAX MAX_PATH
#endif

#define STRINGIFY_IND(x) #x
#define STRINGIFY(x) STRINGIFY_IND(x)
#define MULTILINE_STRING(x) STRINGIFY(x)

#define PANIC_FMT(L, fmt, ...) luaL_error(L, fmt, __VA_ARGS__);
#define PANIC(L, msg) luaL_error(L, "ERROR: " msg)
#define ASSERT(L, b) do { if(!(b)) { PANIC(L, STRINGIFY(b)); } } while(0)

#endif // CONFIG_H
