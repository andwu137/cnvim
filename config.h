#ifndef CONFIG_H
#define CONFIG_H

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

#define PANIC_FMT(L, fmt, ...) luaL_error(L, fmt, __VA_ARGS__)
#define PANIC(L, msg) luaL_error(L, "ERROR: " msg)
#define ASSERT(L, b) do { if(!(b)) { PANIC(L, "assert(" STRINGIFY(b) ")"); } } while(0)

typedef int32_t Codepoint;
#define CODEPOINT_INVALID (Codepoint)(0xfffd)
#define CODEPOINT_MAX     (Codepoint)(0x0010ffff)
#define CODEPOINT_BOM     (Codepoint)(0xfeff)
#define CODEPOINT_EOF     (Codepoint)(-1)

#endif // CONFIG_H
