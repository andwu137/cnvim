/* BEGIN https://github.com/neovim/neovim/blob/master/src/nvim/api/private/defs.h */

#ifndef NVIM_API_C
#define NVIM_API_C

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// BEGIN https://github.com/neovim/neovim/blob/51af2797c2fe0fdb1774d6dd4383d8cbed215df0/src/klib/kvec.h#L53
#define kvec_t(type) \
  struct { \
    size_t size; \
    size_t capacity; \
    type *items; \
  }
// END

// BEGIN https://github.com/neovim/neovim/blob/master/src/nvim/types_defs.h
typedef int LuaRef;
typedef int handle_T;
// END

// BEGIN https://github.com/neovim/neovim/blob/master/src/nvim/memory_defs.h#L12
typedef struct consumed_blk {
  struct consumed_blk *prev;
} *ArenaMem;

typedef struct {
  char *cur_blk;
  size_t pos, size;
} Arena;

#define ARENA_BLOCK_SIZE 4096

// inits an empty arena.
#define ARENA_EMPTY { .cur_blk = NULL, .pos = 0, .size = 0 }
// END

#define ARRAY_DICT_INIT KV_INITIAL_VALUE
#define STRING_INIT { .data = NULL, .size = 0 }
#define OBJECT_INIT { .type = kObjectTypeNil }
#define ERROR_INIT ((Error) { .type = kErrorTypeNone, .msg = NULL })
#define REMOTE_TYPE(type) typedef handle_T type

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

REMOTE_TYPE(Buffer);
REMOTE_TYPE(Window);
REMOTE_TYPE(Tabpage);

typedef struct object Object;
typedef kvec_t(Object) Array;

typedef struct key_value_pair KeyValuePair;
typedef kvec_t(KeyValuePair) Dict;

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
    Array array;
    Dict dict;
    LuaRef luaref;
  } data;
};

typedef uint64_t OptionalKeys;
// END


// BEGIN https://github.com/neovim/neovim/blob/51af2797c2fe0fdb1774d6dd4383d8cbed215df0/src/nvim/api/keysets_defs.h#L165
typedef struct {
  Object scope;
  Object win;
  Object buf;
} Dict(option);

typedef struct {
  OptionalKeys is_set__create_autocmd_;
  Buffer buffer;
  Union(String, LuaRefOf((DictAs(create_autocmd__callback_args) args), *Boolean)) callback;
  String command;
  String desc;
  Union(Integer, String) group;
  Boolean nested;
  Boolean once;
  Union(String, ArrayOf(String)) pattern;
} Dict(create_autocmd);

typedef struct {
  OptionalKeys is_set__create_augroup_;
  Boolean clear;
} Dict(create_augroup);
// END

// BEGIN https://github.com/neovim/neovim/blob/51af2797c2fe0fdb1774d6dd4383d8cbed215df0/src/nvim/api/keysets_defs.h#L93
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
// END

// BEGIN https://github.com/neovim/neovim/blob/master/src/nvim/os/stdpaths_defs.h#L12
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
// END

// BEGIN https://github.com/neovim/neovim/blob/acb99b8a6572d8ea8d917955a653945550923be0/src/nvim/api/private/helpers.h#L67
#define HAS_KEY(d, typ, key) (((d)->is_set__##typ##_ & (1ULL << KEYSET_OPTIDX_##typ##__##key)) != 0)

#define PUT_KEY(d, typ, key, v) \
  do { (d).is_set__##typ##_ |= (1ULL << KEYSET_OPTIDX_##typ##__##key); (d).key = v; } while (0)
// END

// BEGIN build neovim: build/src/nvim/auto/keysets_defs.generated.h
#define KEYSET_OPTIDX_create_autocmd__desc 1
#define KEYSET_OPTIDX_create_autocmd__once 2
#define KEYSET_OPTIDX_create_autocmd__group 3
#define KEYSET_OPTIDX_create_autocmd__buffer 4
#define KEYSET_OPTIDX_create_autocmd__nested 5
#define KEYSET_OPTIDX_create_autocmd__command 6
#define KEYSET_OPTIDX_create_autocmd__pattern 7
#define KEYSET_OPTIDX_create_autocmd__callback 8
// END

// BEGIN build neovim: build/src/nvim/auto/keysets_defs.generated.h
#define KEYSET_OPTIDX_create_augroup__clear 1
// END

// BEGIN build neovim: build/src/nvim/auto/keysets_defs.generated.h
#define KEYSET_OPTIDX_highlight__bg 1
#define KEYSET_OPTIDX_highlight__fg 2
#define KEYSET_OPTIDX_highlight__sp 3
#define KEYSET_OPTIDX_highlight__url 4
#define KEYSET_OPTIDX_highlight__bold 5
#define KEYSET_OPTIDX_highlight__link 6
#define KEYSET_OPTIDX_highlight__blend 7
#define KEYSET_OPTIDX_highlight__cterm 8
#define KEYSET_OPTIDX_highlight__force 9
#define KEYSET_OPTIDX_highlight__italic 10
#define KEYSET_OPTIDX_highlight__special 11
#define KEYSET_OPTIDX_highlight__ctermbg 12
#define KEYSET_OPTIDX_highlight__ctermfg 13
#define KEYSET_OPTIDX_highlight__default 14
#define KEYSET_OPTIDX_highlight__altfont 15
#define KEYSET_OPTIDX_highlight__reverse 16
#define KEYSET_OPTIDX_highlight__fallback 17
#define KEYSET_OPTIDX_highlight__standout 18
#define KEYSET_OPTIDX_highlight__nocombine 19
#define KEYSET_OPTIDX_highlight__undercurl 20
#define KEYSET_OPTIDX_highlight__underline 21
#define KEYSET_OPTIDX_highlight__background 22
#define KEYSET_OPTIDX_highlight__bg_indexed 23
#define KEYSET_OPTIDX_highlight__foreground 24
#define KEYSET_OPTIDX_highlight__fg_indexed 25
#define KEYSET_OPTIDX_highlight__global_link 26
#define KEYSET_OPTIDX_highlight__underdashed 27
#define KEYSET_OPTIDX_highlight__underdotted 28
#define KEYSET_OPTIDX_highlight__underdouble 29
#define KEYSET_OPTIDX_highlight__strikethrough 30
// END

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

extern Buffer nvim_get_current_buf(void);
extern ArrayOf(String) nvim_buf_get_lines(
    uint64_t channel_id, Buffer buffer, Integer start, Integer end, Boolean strict_indexing, Arena *arena, lua_State *lstate, Error *err);

extern String nvim_get_current_line(Arena *arena, Error *err);
extern ArrayOf(Buffer) nvim_list_bufs(Arena *arena);
extern Boolean nvim_buf_is_loaded(Buffer buffer);
extern ArrayOf(Integer, 2) nvim_win_get_cursor(Window window, Arena *arena, Error *err);

extern Integer nvim_create_augroup(
    uint64_t channel_id, String name, Dict(create_augroup) *opts, Error *err);
extern Integer nvim_create_autocmd(
    uint64_t channel_id, Object event, Dict(create_autocmd) *opts, Arena *arena, Error *err);

#endif // NVIM_API_C
