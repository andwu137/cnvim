/* BEGIN https://github.com/neovim/neovim/blob/master/src/nvim/api/private/defs.h */

#ifndef NVIM_API_C
#define NVIM_API_C

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// https://github.com/neovim/neovim/blob/51af2797c2fe0fdb1774d6dd4383d8cbed215df0/src/klib/kvec.h#L53
#define kvec_t(type) \
  struct { \
    size_t size; \
    size_t capacity; \
    type *items; \
  }

typedef int LuaRef; // https://github.com/neovim/neovim/blob/master/src/nvim/types_defs.h

#define ERROR_INIT ((Error) { .type = kErrorTypeNone, .msg = NULL })

#define ArrayOf(...) Array
#define DictOf(...) Dict
#define DictAs(name) Dict
#define Dict(name) KeyDict_##name
#define Enum(...) String
#define DictHash(name) KeyDict_##name##_get_field
#define DictKey(name)
#define LuaRefOf(...) LuaRef
#define Union(...) Object
#define Tuple(...) Array

typedef bool Boolean;
typedef int64_t Integer;
typedef double Float;

typedef Integer HLGroupID;
/* EXCLUDE #include "api/private/defs.h.inline.generated.h" */

// Basic types
typedef enum {
  kErrorTypeNone = -1,
  kErrorTypeException,
  kErrorTypeValidation,
} ErrorType;

typedef struct {
  ErrorType type;
  char *msg;
} Error;

typedef bool Boolean;
typedef int64_t Integer;
typedef double Float;

typedef struct {
  char *data;
  size_t size;
} String;

typedef struct object Object;
typedef kvec_t(Object) Array;

typedef enum {
  kObjectTypeNil = 0,
  kObjectTypeBoolean,
  kObjectTypeInteger,
  kObjectTypeFloat,
  kObjectTypeString,
  kObjectTypeArray,
  kObjectTypeDict,
  kObjectTypeLuaRef,
  // EXT types, cannot be split or reordered, see #EXT_OBJECT_TYPE_SHIFT
  kObjectTypeBuffer,
  kObjectTypeWindow,
  kObjectTypeTabpage,
} ObjectType;

struct object {
  ObjectType type;
  union {
    Boolean boolean;
    Integer integer;
    Float floating;
    String string;
    /* EXCLUDE Array array; */
    /* EXCLUDE Dict dict; */
    /* EXCLUDE LuaRef luaref; */
  } data;
};

typedef uint64_t OptionalKeys;

/* END https://github.com/neovim/neovim/blob/master/src/nvim/api/private/defs.h */

// https://github.com/neovim/neovim/blob/51af2797c2fe0fdb1774d6dd4383d8cbed215df0/src/nvim/api/keysets_defs.h#L165
typedef struct {
  Object scope;
  Object win;
  Object buf;
} KeyDict_option;

// https://github.com/neovim/neovim/blob/51af2797c2fe0fdb1774d6dd4383d8cbed215df0/src/nvim/api/keysets_defs.h#L93
typedef struct {
  OptionalKeys is_set__keymap_;
  Boolean noremap;
  Boolean nowait;
  Boolean silent;
  Boolean script;
  Boolean expr;
  Boolean unique;
  LuaRef callback;
  String desc;
  Boolean replace_keycodes;
} Dict(keymap);

typedef struct {
  OptionalKeys is_set__highlight_;
  Boolean bold;
  Boolean standout;
  Boolean strikethrough;
  Boolean underline;
  Boolean undercurl;
  Boolean underdouble;
  Boolean underdotted;
  Boolean underdashed;
  Boolean italic;
  Boolean reverse;
  Boolean altfont;
  Boolean nocombine;
  Boolean default_ DictKey(default);
  Union(Integer, String) cterm;
  Union(Integer, String) foreground;
  Union(Integer, String) fg;
  Union(Integer, String) background;
  Union(Integer, String) bg;
  Union(Integer, String) ctermfg;
  Union(Integer, String) ctermbg;
  Union(Integer, String) special;
  Union(Integer, String) sp;
  HLGroupID link;
  HLGroupID global_link;
  Boolean fallback;
  Integer blend;
  Boolean fg_indexed;
  Boolean bg_indexed;
  Boolean force;
  String url;
} Dict(highlight);

// https://github.com/neovim/neovim/blob/master/src/nvim/os/stdpaths_defs.h#L12
typedef enum {
  kXDGNone = -1,
  kXDGConfigHome,  ///< XDG_CONFIG_HOME
  kXDGDataHome,    ///< XDG_DATA_HOME
  kXDGCacheHome,   ///< XDG_CACHE_HOME
  kXDGStateHome,   ///< XDG_STATE_HOME
  kXDGRuntimeDir,  ///< XDG_RUNTIME_DIR
  kXDGConfigDirs,  ///< XDG_CONFIG_DIRS
  kXDGDataDirs,    ///< XDG_DATA_DIRS
} XDGVarType;

/* API Functions */
extern int do_cmdline_cmd(const char *cmd);

extern char *get_xdg_home(const int);
extern char *stdpaths_user_conf_subpath(const char *fname);
extern char *stdpaths_user_data_subpath(const char *fname);

extern bool os_isdir(const char *name);
extern char *runtimepath_default(bool clean_arg);

extern void nvim_set_option_value(uint64_t channel_id, String name, Object value, Dict(option) * opts, Error *err);

extern void nvim_set_var(String name, Object value, Error *err);
extern void nvim_set_keymap(uint64_t channel_id, String mode, String lhs, String rhs, Dict(keymap) * opts, Error *err);
extern void nvim_set_hl(uint64_t channel_id, Integer ns_id, String name, Dict(highlight) *val, Error *err);

#endif // NVIM_API_C
