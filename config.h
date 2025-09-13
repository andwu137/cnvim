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
#define ASSERT(L, b) do { if(!(b)) { PANIC(L, "assert(" STRINGIFY(b) ")"); } } while(0)

#define STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

typedef int32_t Codepoint;
#define CODEPOINT_INVALID (Codepoint)(0xfffd)
#define CODEPOINT_MAX     (Codepoint)(0x0010ffff)
#define CODEPOINT_BOM     (Codepoint)(0xfeff)
#define CODEPOINT_EOF     (Codepoint)(-1)

#endif // CONFIG_H
