// TODO: replace any `do_cmdline_cmd` if possible
// TODO: test if the plugins/binds actually work
// TODO: refactor locality of option settings
// TODO: more asserts on lua types
// TODO: get ref for function keybinds
// TODO: keep vim and deps add on the stack

#include <ctype.h>
#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvim_api.c"

#include "config.h"
#include "arena.c"
#include "fileio.c"

/* TYPES */
#if PERFORMANCE
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

static char const *g_perf_time_strings[] =
{
#define PERF_TIME_X(n) #n,
  PERF_TIME_LIST
#undef PERF_TIME_X
};

#if defined(__linux__)
  #include <time.h>
#else // __linux__
  #error "OS not supported yet"
#endif // OS
#endif // PERFORMANCE

/* GLOBALS */
static char const g_package_dir[] = "site/";
static char const g_mini_plugin_dir[] = "pack/deps/opt/mini.nvim";
static char const g_install_mini_nvim_command[] =
  "git clone --filter=blob:none https://github.com/nvim-mini/mini.nvim ";
static uint8_t g_lua_macro_latch;

static char const *g_lsp_servers[] =
{
  "lua_ls",
  "clangd",
  "ols",
};

/* HELPERS */
// Variable Type Constructors
static inline String
nvim_mk_string(
    char *s)
{
  return (String){ .data = s, .size = strlen(s) };
}

static inline String
nvim_mk_string_from_slice(
    char *s,
    uint s_len)
{
  return (String){ .data = s, .size = s_len };
}

static inline Object
nvim_mk_obj_bool(
    bool b)
{
  return (Object){ .type = kObjectTypeBoolean, .data.boolean = b };
}

static inline Object
nvim_mk_obj_luaref(
    LuaRef r)
{
  return (Object){ .type = kObjectTypeLuaRef, .data.luaref = r };
}

static inline Object
nvim_mk_obj_int(
    Integer i)
{
  return (Object){ .type = kObjectTypeInteger, .data.integer = i };
}

static inline Object
nvim_mk_obj_string(
    char *s)
{
  return (Object)
  {
    .type = kObjectTypeString, .data.string = (String)
    {
      .data = s,
      .size = strlen(s),
    }
  };
}

static inline Object
nvim_mk_obj_string_from_slice(
    char *s,
    uint s_len)
{
  return (Object)
  {
    .type = kObjectTypeString, .data.string = (String)
    {
      .data = s,
      .size = s_len,
    }
  };
}

// Variable Setters
static inline void
nvim_set_g(
    lua_State *L,
    char *key,
    Object val)
{
  Error e = ERROR_INIT;
  nvim_set_var(nvim_mk_string(key), val, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

static inline void
nvim_set_o(
    lua_State *L,
    char *key,
    Object val)
{
  Dict(option) o = {0};
  Error e = ERROR_INIT;
  nvim_set_option_value(0, nvim_mk_string(key), val, &o, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

static inline Object
nvim_get_o(
    lua_State *L,
    char *key)
{
  Dict(option) o = {0};
  Error e = ERROR_INIT;
  Object out = nvim_get_option_value(nvim_mk_string(key), &o, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
  return out;
}

// Key Mapping
static inline void
nvim_map_bufnr(
    lua_State *L,
    Buffer bufnr,
    char *mode,
    char *key,
    char *action)
{
  Dict(keymap) o = {0};
  PUT_KEY(o, keymap, noremap, true);
  PUT_KEY(o, keymap, silent, true);
  Error e = ERROR_INIT;
  nvim_buf_set_keymap(0, bufnr, nvim_mk_string(mode), nvim_mk_string(key), nvim_mk_string(action), &o, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

static inline void
nvim_map(
    lua_State *L,
    char *mode,
    char *key,
    char *action)
{
  Dict(keymap) o = {0};
  PUT_KEY(o, keymap, noremap, true);
  PUT_KEY(o, keymap, silent, true);
  Error e = ERROR_INIT;
  nvim_set_keymap(0, nvim_mk_string(mode), nvim_mk_string(key), nvim_mk_string(action), &o, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

#define NVIM_MAP_CMD(L, mode, key, action) nvim_map(L, mode, key, "<cmd>" action "<cr>")

static inline void
nvim_highlight(
    lua_State *L,
    char *group,
    Dict(highlight) opts)
{
  Error e = ERROR_INIT;
  nvim_set_hl(0, 0, nvim_mk_string(group), &opts, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

// Auto Cmds
static inline Integer
nvim_mk_autocmd_callback(
    lua_State *L,
    char *name,
    char *desc,
    char *augroup_name,
    bool augroup_clear,
    Union(String, LuaRefOf((DictAs(create_autocmd__callback_args) args), *Boolean)) callback)
{
  Arena arena = ARENA_EMPTY;
  Error e = ERROR_INIT;

  Dict(create_augroup) augroup = {0};
  PUT_KEY(augroup, create_augroup, clear, augroup_clear);

  Dict(create_autocmd) autocmd = {0};
  PUT_KEY(autocmd, create_autocmd, desc, nvim_mk_string(desc));
  PUT_KEY(autocmd, create_autocmd, group,
      nvim_mk_obj_int(nvim_create_augroup(0, nvim_mk_string(augroup_name), &augroup, &e)));
  PUT_KEY(autocmd, create_autocmd, callback, callback);

  Integer n = nvim_create_autocmd(0, nvim_mk_obj_string(name), &autocmd, &arena, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
  return n;
}

#define NVIM_MK_AUTOCMD_CALLBACK(L, name, desc, augroup_name, augroup_clear, callback) do { \
  lsp_disable_semantic_highlights(L); \
  lua_register(L, "g_" STRINGIFY(callback), callback); \
  lua_getglobal(L, "g_" STRINGIFY(callback)); \
  int lua_ref_##callback = luaL_ref(L, LUA_REGISTRYINDEX); \
  nvim_mk_autocmd_callback(L, name, desc, augroup_name, augroup_clear, \
      nvim_mk_obj_luaref(lua_ref_##callback)); \
} while(0)

static inline Integer
nvim_mk_autocmd_command(
    lua_State *L,
    char *name,
    char *desc,
    char *augroup_name,
    bool augroup_clear,
    String command)
{
  Arena arena = ARENA_EMPTY;
  Error e = ERROR_INIT;

  Dict(create_augroup) augroup = {0};
  PUT_KEY(augroup, create_augroup, clear, augroup_clear);

  Dict(create_autocmd) autocmd = {0};
  PUT_KEY(autocmd, create_autocmd, desc, nvim_mk_string(desc));
  PUT_KEY(autocmd, create_autocmd, group,
      nvim_mk_obj_int(nvim_create_augroup(0, nvim_mk_string(augroup_name), &augroup, &e)));
  PUT_KEY(autocmd, create_autocmd, command, command);

  Integer n = nvim_create_autocmd(0, nvim_mk_obj_string(name), &autocmd, &arena, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
  return n;
}

// lua debug
static inline void
mlua_stack_dump(
    lua_State *L,
    FILE *fptr)
{
  int top = lua_gettop(L);
  for(int i = 1;
      i <= top;
      i += 1)
  {
    fprintf(fptr, "%d\t%s\t", i, luaL_typename(L, i));
    switch (lua_type(L, i))
    {
    case LUA_TNUMBER: { fprintf(fptr, "%g\n", lua_tonumber(L, i)); } break;
    case LUA_TSTRING: { fprintf(fptr, "%s\n", lua_tostring(L, i)); } break;
    case LUA_TBOOLEAN: { fprintf(fptr, "%s\n", (lua_toboolean(L, i) ? "true" : "false")); } break;
    case LUA_TNIL: { fprintf(fptr, "%s\n", "nil"); } break;
    default: { fprintf(fptr, "%p\n", lua_topointer(L, i)); } break;
    }
  }
}

static inline void
mlua_stack_dump_temp(
    lua_State *L)
{
  FILE *fptr = fopen(".stack_dump", "w");
  mlua_stack_dump(L, fptr);
  fclose(fptr);
}

// lua api
#define MLUA_PCALL(L, in, out) ASSERT(L, lua_pcall(L, in, out, 0) == 0)

#define MLUA_PCALL_VOID(L, in) do { MLUA_PCALL(L, in, 0); lua_pop(L, 1); } while(0)

#define MLUA_SELF(L, f) do { lua_pushvalue(L, -1); lua_getfield(L, -1, f); lua_insert(L, -2); } while(0)

#define MLUA_SELF_PCALL(L, f, in, out) do { MLUA_SELF(L, f); MLUA_PCALL(L, in, out); } while(0)

#define MLUA_SELF_PCALL_VOID(L, f, in) do { MLUA_SELF(L, f); MLUA_PCALL_VOID(L, in); } while(0)

#define MLUA_REQUIRE(L, name) do { lua_getglobal(L, "require"); lua_pushstring(L, name); MLUA_PCALL(L, 1, 1); } while(0)

#define MLUA_REQUIRE_SETUP(L, name) do { MLUA_REQUIRE(L, name); ASSERT(L, lua_istable(L, -1)); lua_getfield(L, -1, "setup"); } while(0)

#define MLUA_REQUIRE_SETUP_CALL(L, name) do { MLUA_REQUIRE_SETUP(L, name); MLUA_PCALL_VOID(L, 0); } while(0)

#define MLUA_REQUIRE_SETUP_TABLE_CALL(L, name) do { MLUA_REQUIRE_SETUP(L, name); lua_createtable(L, 0, 0); MLUA_PCALL_VOID(L, 1); } while(0)

#define MLUA_REQUIRE_SETUP_TABLE(L, name, an, tn) \
  for( \
      g_lua_macro_latch = 1, \
        lua_getglobal(L, "require"), \
        lua_pushstring(L, name), \
        MLUA_PCALL(L, 1, 1), \
        ASSERT(L, lua_istable(L, -1)), \
        lua_getfield(L, -1, "setup"), \
        lua_createtable(L, an, tn); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        MLUA_PCALL(L, 1, 0), \
        lua_pop(L, 1))

#define MLUA_MINIDEPS_ADD(L, an, tn) \
  for( \
      g_lua_macro_latch = 1, \
        lua_getglobal(L, "MiniDeps"), \
        ASSERT(L, lua_istable(L, -1)), \
        lua_getfield(L, -1, "add"), \
        lua_createtable(L, an, tn); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        MLUA_PCALL(L, 1, 0), \
        lua_pop(L, 1))

#define MLUA_PUSH_KV(L, k) \
  for( \
      g_lua_macro_latch = 1, \
        lua_pushstring(L, k); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        lua_settable(L, -3))

#define MLUA_PUSH_KV_TABLE(L, k, an, tn) \
  for( \
      g_lua_macro_latch = 1, \
        lua_pushstring(L, k), \
        lua_createtable(L, an, tn); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        lua_settable(L, -3))

#define MLUA_PUSH_KV_TABLE_KV(L, k1, k2) \
  for( \
      g_lua_macro_latch = 1, \
        lua_pushstring(L, k1), \
        lua_createtable(L, 0, 1), \
        lua_pushstring(L, k2); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        lua_settable(L, -3), \
        lua_settable(L, -3))

#define MLUA_PUSH_KV_TABLE_IDX(L, k) \
  for( \
      g_lua_macro_latch = 1, \
        lua_pushstring(L, k), \
        lua_createtable(L, 1, 0); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        lua_rawseti(L, -2, 1), \
        lua_settable(L, -3))

#define MLUA_PUSH_IDX(L, i) \
  for( \
      g_lua_macro_latch = 1; \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        lua_rawseti(L, -2, i))

#define MLUA_PUSH_IDX_TABLE(L, i, an, tn) \
  for( \
      g_lua_macro_latch = 1, \
        lua_createtable(L, an, tn); \
      g_lua_macro_latch; \
      g_lua_macro_latch = 0, \
        lua_rawseti(L, -2, i))

/* MAIN */
static struct { char *filetype; char *comment; } const g_mini_comment_custom_commentstring_strings[] =
{
  {"v", "/* %s */"},
};

int
mini_comment_custom_commentstring(
    lua_State *L)
{
  lua_getglobal(L, "vim");
  lua_getfield(L, -1, "bo");
  lua_getfield(L, -1, "ft");
  ASSERT(L, lua_isstring(L, -1));
  char const *ft = lua_tostring(L, -1);
  lua_pop(L, 4);

  for(int i = 0;
      i < (int)STATIC_ARRAY_SIZE(g_mini_comment_custom_commentstring_strings);
      i += 1)
  {
    if(strcmp(ft, g_mini_comment_custom_commentstring_strings[i].filetype)
        == 0)
    {
      lua_pushlstring(
          L,
          g_mini_comment_custom_commentstring_strings[i].comment,
          sizeof(g_mini_comment_custom_commentstring_strings[i].comment));
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int
mini_pick_window_config(
    lua_State *L)
{
  Object lines = nvim_get_o(L, "lines");
  Object columns = nvim_get_o(L, "columns");
  ASSERT(L, lines.type == kObjectTypeInteger);
  ASSERT(L, columns.type == kObjectTypeInteger);
  lua_createtable(L, 0, 4);
  {
    MLUA_PUSH_KV(L, "row") { lua_pushnumber(L, 0); }
    MLUA_PUSH_KV(L, "col") { lua_pushnumber(L, 0); }
    MLUA_PUSH_KV(L, "height") { lua_pushinteger(L, lines.data.integer); }
    MLUA_PUSH_KV(L, "width") { lua_pushinteger(L, columns.data.integer); }
  }
  return 1;
}

int
mini_pick_choose_all(
    lua_State *L)
{
  lua_getglobal(L, "MiniPick");
  lua_getfield(L, -1, "get_picker_opts");
  MLUA_PCALL(L, 0, 1);
  lua_getfield(L, -1, "mappings");
  int mappings_idx = lua_gettop(L);

  lua_getfield(L, mappings_idx, "mark_all");
  lua_getfield(L, mappings_idx, "choose_marked");

  lua_getglobal(L, "vim");
  lua_getfield(L, -1, "api");
  lua_getfield(L, -1, "nvim_input");
  lua_pushfstring(L, "%s%s", lua_tostring(L, mappings_idx + 1), lua_tostring(L, mappings_idx + 2));
  MLUA_PCALL_VOID(L, 1);

  return 0;
}

int
conform_formatters_by_ft_python(
    lua_State *L)
{
  MLUA_REQUIRE(L, "conform");
  lua_getfield(L, -1, "get_formatter_info");
    lua_pushstring(L, "ruff_format");
    lua_pushvalue(L, -4);
    MLUA_PCALL(L, 2, 1);

    ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "available");

  ASSERT(L, lua_isboolean(L, -1));
  bool has_ruff = lua_toboolean(L, -1);
  lua_pop(L, 4);

  lua_createtable(L, 2, 0);
  if(has_ruff)
  {
    MLUA_PUSH_IDX(L, 1) { lua_pushstring(L, "ruff_format"); };
  }
  else
  {
    MLUA_PUSH_IDX(L, 1) { lua_pushstring(L, "isort"); }
    MLUA_PUSH_IDX(L, 2) { lua_pushstring(L, "black"); }
  }

  return 1;
}

int
lsp_on_attach(
    lua_State *L)
{
  int nargs = lua_gettop(L);
  if(nargs < 1)
  {
    PANIC(L, "lsp_on_attach: expected at least 1 arg");
    return 0;
  }

  lua_getfield(L, -1, "buf");
  Buffer bufnr = lua_tointeger(L, -1);

  // setup lsp omnifunc completion
  lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "bo"); ASSERT(L, lua_istable(L, -1));
  lua_pushinteger(L, bufnr);
  lua_gettable(L, -2);
  lua_pushstring(L, "v:lua.vim.lsp.omnifunc");
  lua_setfield(L, -2, "omnifunc");
  lua_pop(L, 2);

  // setup lsp completion
  lua_getfield(L, -1, "lsp"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "get_client_by_id");
  lua_getfield(L, 1, "data"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "client_id");
  lua_remove(L, -2);
  MLUA_PCALL(L, 1, 1);

  MLUA_SELF(L, "supports_method");
  lua_pushstring(L,"textDocument/completion");
  MLUA_PCALL(L, 2, 1);
  if(lua_toboolean(L, -1))
  {
    lua_getfield(L, 4, "completion"); ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "enable");
    lua_pushboolean(L, true);
    lua_getfield(L, 5, "id");
    lua_pushvalue(L, 2);
    MLUA_PCALL_VOID(L, 3);
    NVIM_MAP_CMD(L, "i", "<c-space>", "lua vim.lsp.completion.get()");
  }

  // lsp keybinds
  nvim_map_bufnr(L, bufnr, "n", "gD", "<cmd>lua vim.lsp.buf.declaration()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "gd", "<cmd>lua vim.lsp.buf.definition()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "gi", "<cmd>lua vim.lsp.buf.implementation()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "gr", "<cmd>lua vim.lsp.buf.references()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "K", "<cmd>lua vim.lsp.buf.hover()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "<c-k>", "<cmd>lua vim.lsp.buf.signature_help()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "<leader>cr", "<cmd>lua vim.lsp.buf.rename()<cr>");
  nvim_map_bufnr(L, bufnr, "n", "<leader>ca", "<cmd>lua vim.lsp.buf.code_action()<cr>");
  return 0;
}

int
lsp_disable_semantic_highlights(
    lua_State *L)
{
  lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "fn"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "getcompletion");
  lua_pushstring(L, "@lsp");
  lua_pushstring(L, "highlight");
  MLUA_PCALL(L, 2, 1);

  int table_idx = lua_gettop(L);
  Dict(highlight) hl = {0};
  for(lua_pushnil(L);
      lua_next(L, table_idx);
      lua_pop(L, 1))
  {
    nvim_highlight(L, (char *)lua_tostring(L, -1), hl);
  }

  lua_pop(L, 3);
  return 0;
}

int
treesitter_update(
    lua_State *L)
{
  (void)L;
  do_cmdline_cmd("TSUpdate");
  return 0;
}

int
disable_conceallevel(
    lua_State *L)
{
  nvim_set_o(L, "conceallevel", nvim_mk_obj_int(0));
  return 0;
}

int
luaopen_config(
    lua_State *L)
{
#if PERFORMANCE
  struct timespec perf_times[Perf_Time_Count][2] = {0};

  START_PERF_TIME(perf_times, Perf_Time_Total);

  perf_times[Perf_Time_Path][0] = perf_times[Perf_Time_Total][0];
#endif
  // setup command string creator, useful for temporary strings
  struct Arena string_arena;
  ASSERT(L, init_arena(&string_arena, 4096 * 4)); // arbitrary size

  // RUNTIME
  char *package_path = stdpaths_user_data_subpath(g_package_dir);
  uint package_path_len = strlen(package_path);
  ASSERT(L, package_path_len > 0);

  // get runtime path
  char *runtimepath_default_string = runtimepath_default(false);
  uint runtimepath_default_string_len = strnlen(runtimepath_default_string, string_arena.capacity);
  ASSERT(L, runtimepath_default_string_len > 0);

  // create the new runtimepath
  ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)runtimepath_default_string, runtimepath_default_string_len));
  ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)",", 1));

  char *plugin_path = string_arena.buffer + string_arena.length;

  ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)package_path, package_path_len));

  uint plugin_path_len = string_arena.buffer + string_arena.length - plugin_path;
  ASSERT(L, plugin_path_len > 0);

  // set runtimepath
  nvim_set_o(L, "runtimepath",
      nvim_mk_obj_string_from_slice(string_arena.buffer, string_arena.length));

  /* SETUP THE PACKAGE MANAGER */
  // setup plugin_path for mini.nvim
  ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)g_mini_plugin_dir, sizeof(g_mini_plugin_dir) - 1));

  // try installing mini.deps
  if(!os_isdir(plugin_path))
  {
    ASSERT(L, replace_range_arena(
          &string_arena,
          0, plugin_path - string_arena.buffer,
          (uint8_t *)g_install_mini_nvim_command, sizeof(g_install_mini_nvim_command) - 1));

    plugin_path = string_arena.buffer + sizeof(g_install_mini_nvim_command) - 1;

    // null terminate
    ASSERT(L, string_arena.length < string_arena.capacity);
    string_arena.buffer[string_arena.length] = 0;

    system(string_arena.buffer);
  }
  clear_arena(&string_arena);

  // require the package manager
  do_cmdline_cmd("packadd mini.nvim | helptags ALL");
  MLUA_REQUIRE_SETUP_TABLE(L, "mini.deps", 0, 1)
  {
    MLUA_PUSH_KV_TABLE_KV(L, "path", "package") { lua_pushlstring(L, plugin_path, plugin_path_len); }
  }

#if PERFORMANCE
  END_PERF_TIME(perf_times, Perf_Time_Path);

  perf_times[Perf_Time_Opt][0] = perf_times[Perf_Time_Path][1];
#endif

  /* OPTIONS */
  nvim_set_g(L, "mapleader", nvim_mk_obj_string(" "));
  nvim_set_g(L, "maplocalleader", nvim_mk_obj_string(","));

  // Make line numbers default
  nvim_set_o(L, "number", nvim_mk_obj_bool(true));
  nvim_set_o(L, "relativenumber", nvim_mk_obj_bool(true));

  // Enable mouse mode (useful for resizing splits)
  nvim_set_o(L, "mouse", nvim_mk_obj_string("a"));

  // Save undo history
  nvim_set_o(L, "undofile", nvim_mk_obj_bool(true));

  // Case-insensitive searching UNLESS \C or one or more capital letters in the search term
  nvim_set_o(L, "ignorecase", nvim_mk_obj_bool(true));
  nvim_set_o(L, "smartcase", nvim_mk_obj_bool(true));

  // Keep signcolumn on by default
  nvim_set_o(L, "signcolumn", nvim_mk_obj_string("yes"));

  // Decrease update time
  nvim_set_o(L, "updatetime", nvim_mk_obj_int(50));

  // Increase time before timeout
  nvim_set_o(L, "timeout", nvim_mk_obj_bool(true));
  nvim_set_o(L, "timeoutlen", nvim_mk_obj_int(5'000));

  // Configure how new splits should be opened
  nvim_set_o(L, "splitright", nvim_mk_obj_bool(true));
  nvim_set_o(L, "splitbelow", nvim_mk_obj_bool(true));

  //  See `:help 'list'`
  //  and `:help 'listchars'`
  nvim_set_o(L, "list", nvim_mk_obj_bool(true));
  nvim_set_o(L, "listchars", nvim_mk_obj_string("tab:» ,trail:·,nbsp:␣"));

  // Preview substitutions live, as you type!
  nvim_set_o(L, "inccommand", nvim_mk_obj_string("split"));

  // Show which line your cursor is on
  // nvim_set_o(L, "cursorline", nvim_mk_obj_bool(true));

  // Minimal number of screen lines to keep above and below the cursor.
  nvim_set_o(L, "scrolloff", nvim_mk_obj_int(1));
  nvim_set_o(L, "sidescrolloff", nvim_mk_obj_int(20));

  // Tabs
  nvim_set_o(L, "tabstop", nvim_mk_obj_int(4));
  nvim_set_o(L, "softtabstop", nvim_mk_obj_int(4));
  nvim_set_o(L, "shiftwidth", nvim_mk_obj_int(4));
  // nvim_set_o(L, "expandtab", nvim_mk_obj_bool(true));

  // Dont pass this marker
  nvim_set_o(L, "colorcolumn", nvim_mk_obj_string("80"));

  // Linewraps
  nvim_set_o(L, "showbreak", nvim_mk_obj_string("└▶"));
  nvim_set_o(L, "wrap", nvim_mk_obj_bool(false));

  // Enable break indent
  nvim_set_o(L, "breakindent", nvim_mk_obj_bool(true));

  // Disable cursor changing
  nvim_set_o(L, "guicursor", nvim_mk_obj_string("n-v-c:block"));

  // cinoptions
  {
    Object cino_obj = nvim_get_o(L, "cinoptions");
    ASSERT(L, cino_obj.type == kObjectTypeString);
    String cinoptions = cino_obj.data.string;
    ASSERT(L, cinoptions.size < string_arena.capacity);
    copy_alloc_arena(&string_arena, (uint8_t *)cinoptions.data, cinoptions.size);
    copy_alloc_arena(&string_arena, (uint8_t *)":0,l1", sizeof(":0,l1") - 1);
    nvim_set_o(L, "cinoptions",
        nvim_mk_obj_string_from_slice(string_arena.buffer, string_arena.length));
    clear_arena(&string_arena);
  }

  // conceal options (syntax visibility)
  NVIM_MK_AUTOCMD_CALLBACK(
      L, "BufEnter", // NOTE: maybe you would put this on another event
                     // ... but, the once flag is not exposed in my api lol
      "Disable Conceal on All Buffers", "my-conceallevel", true,
      disable_conceallevel);

  // completion
  nvim_set_o(L, "wildmode", nvim_mk_obj_string("longest:full"));
  nvim_set_o(L, "wildmenu", nvim_mk_obj_bool(true));

  nvim_set_o(L, "completeopt", nvim_mk_obj_string("longest,noselect"));

  /* Keymaps */
  // Auto Completion
  nvim_map(L, "i", "<c-space>", "<c-x><c-o>");

  // Terminal Binds
  nvim_map(L, "t", "<C-w>", "<c-\\><c-n>");

  // Navigation
  /// Center Screen When Scrolling
  nvim_map(L, "n", "<C-d>", "<C-d>zz");
  nvim_map(L, "n", "<C-u>", "<C-u>zz");

  /// Navigate Wrapped Lines
  nvim_map(L, "n", "j", "gj");
  nvim_map(L, "n", "k", "gk");

  // Yanks
  nvim_map(L, "n", "<leader>y", "\"+y");
  nvim_map(L, "v", "<leader>y", "\"+y");
  nvim_map(L, "n", "<leader>Y", "\"+Y");

  // Set highlight on search, but clear on pressing <Esc> in normal mode
  nvim_set_o(L, "hlsearch", nvim_mk_obj_bool(true));
  nvim_map(L, "n", "<esc>", "<cmd>nohlsearch<cr><esc>");

  // Diagnostics
  NVIM_MAP_CMD(L, "n", "<leader>uq", "copen");
  NVIM_MAP_CMD(L, "n", "<leader>ul", "lopen");

  // Code
  NVIM_MAP_CMD(L, "n", "<leader>cw", "cd %:p:h"); // move nvim base path to current buffer
  NVIM_MAP_CMD(L, "n", "<leader>cm", "Man");
  NVIM_MAP_CMD(L, "n", "<leader>cW", "%s/\\s\\+$//g"); // remove trailing whitespace

  // Make
  NVIM_MAP_CMD(L, "n", "<leader>mm", "make");
  NVIM_MAP_CMD(L, "n", "<leader>mc",
    "lua vim.ui.input({ prompt = 'Make Command: ', default = vim.o.makeprg }, "
      "function(usr_input) vim.o.makeprg = usr_input end)");

  // UI
  NVIM_MAP_CMD(L, "n", "<leader>um", "messages");

  // Toggle
  NVIM_MAP_CMD(L, "n", "<leader>th", "ColorizerToggle");

  // Windows
  NVIM_MAP_CMD(L, "n", "<leader>v", "vsp");
  NVIM_MAP_CMD(L, "n", "<leader>wv", "vsp");
  NVIM_MAP_CMD(L, "n", "<leader>x", "sp");
  NVIM_MAP_CMD(L, "n", "<leader>wx", "sp");
  NVIM_MAP_CMD(L, "n", "<leader>wt", "tab split");

  NVIM_MAP_CMD(L, "n", "<leader>w|", "vertical resize");
  NVIM_MAP_CMD(L, "n", "<leader>w_", "horizontal resize");
  NVIM_MAP_CMD(L, "n", "<leader>ws", "wincmd =");
  nvim_map(L, "n", "<leader>wf", "<cmd>horizontal resize<cr><cmd>vertical resize<cr>");

  NVIM_MAP_CMD(L, "n", "<leader>wN", "setlocal buftype=nofile"); // turn off ability to save

  /* End Keymaps */

  // Highlight when yanking (copying) text
  nvim_mk_autocmd_command(L, "TextYankPost", "Highlight when yanking text", "my-highlight-yank", true,
      nvim_mk_string("lua vim.highlight.on_yank({ on_visual = false })"));

#if PERFORMANCE
  END_PERF_TIME(perf_times, Perf_Time_Opt);

  perf_times[Perf_Time_Download][0] = perf_times[Perf_Time_Opt][1];
#endif

  /* Download Packages */
  // 'a' and 'i' text movements
  MLUA_REQUIRE_SETUP_TABLE(L, "mini.ai", 0, 1)
  {
    MLUA_PUSH_KV(L, "n_lines") { lua_pushinteger(L, 500); }
  }

  // split / join arguments for section
  MLUA_REQUIRE_SETUP_TABLE(L, "mini.splitjoin", 0, 1)
  {
    MLUA_PUSH_KV_TABLE(L, "mappings", 0, 1)
    {
      MLUA_PUSH_KV(L, "toggle") { lua_pushstring(L, "<leader>cS"); }
    }
  }

  // square brackets to move back and forth, between more tag types
  MLUA_REQUIRE_SETUP_CALL(L, "mini.bracketed");

  // custom comment functions
  MLUA_REQUIRE_SETUP_TABLE(L, "mini.comment", 0, 2)
  {
    MLUA_PUSH_KV(L, "ignore_blank_line") { lua_pushboolean(L, true); }

    MLUA_PUSH_KV_TABLE_KV(L, "options", "custom_commentstring") { lua_pushcfunction(L, mini_comment_custom_commentstring); }
  }

  // trailing spaces are highlighted
  MLUA_REQUIRE_SETUP_CALL(L, "mini.trailspace");

  // detect tabbing
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/tpope/vim-sleuth"); }
  }

  // git signs
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/lewis6991/gitsigns.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "gitsigns", 0, 1)
  {
    MLUA_PUSH_KV_TABLE(L, "signs", 0, 5)
    {
      MLUA_PUSH_KV_TABLE_KV(L, "add", "text") { lua_pushstring(L, "+"); }
      MLUA_PUSH_KV_TABLE_KV(L, "change", "text") { lua_pushstring(L, "~"); }
      MLUA_PUSH_KV_TABLE_KV(L, "delete", "text") { lua_pushstring(L, "_"); }
      MLUA_PUSH_KV_TABLE_KV(L, "topdelete", "text") { lua_pushstring(L, "‾"); }
      MLUA_PUSH_KV_TABLE_KV(L, "changedelete", "text") { lua_pushstring(L, "~"); }
    }
  }

  // git signs hunks
  NVIM_MAP_CMD(L, "n", "]h", "lua require('gitsigns').nav_hunk('next')");
  NVIM_MAP_CMD(L, "n", "[h", "lua require('gitsigns').nav_hunk('prev')");

  // file explorer
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/stevearc/oil.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "oil", 0, 5)
  {
    static char const *nvim_oil_columns[] =
    {
      // "permissions",
      // "size",
      "mtime",
      "icon",
    };
    int nvim_oil_columns_length = (int)STATIC_ARRAY_SIZE(nvim_oil_columns);

    MLUA_PUSH_KV_TABLE(L, "columns", nvim_oil_columns_length, 0)
    {
      for(int i = 0;
          i < nvim_oil_columns_length;
          i += 1)
      {
        MLUA_PUSH_IDX(L, i + 1) { lua_pushstring(L, nvim_oil_columns[i]); }
      }
    }

    MLUA_PUSH_KV(L, "natural_order") { lua_pushboolean(L, true); }
    MLUA_PUSH_KV(L, "delete_to_trash") { lua_pushboolean(L, true); }

    MLUA_PUSH_KV_TABLE_KV(L, "view_options", "show_hidden") { lua_pushboolean(L, true); }

    MLUA_PUSH_KV_TABLE(L, "keymaps", 0, 8)
    {
      MLUA_PUSH_KV(L, "g?") { lua_pushstring(L, "actions.show_help"); }
      MLUA_PUSH_KV(L, "<CR>") { lua_pushstring(L, "actions.select"); }
      MLUA_PUSH_KV(L, "L") { lua_pushstring(L, "actions.select"); }
      MLUA_PUSH_KV(L, "H") { lua_pushstring(L, "actions.parent"); }
      MLUA_PUSH_KV(L, "<C-c>") { lua_pushstring(L, "actions.close"); }
      MLUA_PUSH_KV(L, "<C-l>") { lua_pushstring(L, "actions.refresh"); }
      MLUA_PUSH_KV(L, "g.") { lua_pushstring(L, "actions.toggle_hidden"); }
      MLUA_PUSH_KV(L, "g\\") { lua_pushstring(L, "actions.toggle_trash"); }
    }
  }

  NVIM_MAP_CMD(L, "n", "<leader>uf", "Oil");

  // Visual Undo Tree
  MLUA_MINIDEPS_ADD(L, 0, 2)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/jiaoshijie/undotree"); }
    MLUA_PUSH_KV_TABLE_IDX(L, "depends") { lua_pushstring(L, "nvim-lua/plenary.nvim"); }
  }

  MLUA_REQUIRE_SETUP_CALL(L, "undotree");
  NVIM_MAP_CMD(L, "n", "<leader>cu", "lua require('undotree').toggle()");

  // show keybinds
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/folke/which-key.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "which-key", 0, 1)
  {
    MLUA_PUSH_KV(L, "delay") { lua_pushinteger(L, 300); }
  }

  // search engine
  MLUA_REQUIRE_SETUP_TABLE(L, "mini.pick", 0, 1)
  {
    MLUA_PUSH_KV_TABLE_KV(L, "window", "config") { lua_pushcfunction(L, mini_pick_window_config); }
    MLUA_PUSH_KV_TABLE(L, "mappings", 0, 1)
    {
      MLUA_PUSH_KV_TABLE(L, "choose_all", 0, 2)
      {
        MLUA_PUSH_KV(L, "char") { lua_pushstring(L, "<C-q>"); }
        MLUA_PUSH_KV(L, "func") { lua_pushcfunction(L, mini_pick_choose_all); }
      }
    }
  }

  NVIM_MAP_CMD(L, "n", "<leader>sf", "Pick files");
  NVIM_MAP_CMD(L, "n", "<leader>sg", "Pick grep_live");
  NVIM_MAP_CMD(L, "n", "<leader>so", "Pick buffers");
  NVIM_MAP_CMD(L, "n", "<leader>sn",
      "lua MiniPick.start({ source = { cwd = vim.fn.stdpath('config') } }))");
  NVIM_MAP_CMD(L, "n", "<leader>sm",
      "lua MiniPick.start({ source = { items = "
      "vim.fn.systemlist('man -k ' .. vim.fn.input('Man page: ')) } })");

  // qol improvements for marks
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/chentoast/marks.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "marks", 0, 2)
  {
    MLUA_PUSH_KV(L, "default_mappings") { lua_pushboolean(L, true); }

    MLUA_PUSH_KV_TABLE(L, "mappings", 0, 1) { }
  }

  // jump to files
  MLUA_MINIDEPS_ADD(L, 0, 3)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/ThePrimeagen/harpoon"); }
    MLUA_PUSH_KV(L, "checkout") { lua_pushstring(L, "harpoon2"); }

    MLUA_PUSH_KV_TABLE_IDX(L, "depends") { lua_pushstring(L, "nvim-lua/plenary.nvim"); }
  }

  MLUA_REQUIRE(L, "harpoon"); MLUA_SELF_PCALL_VOID(L, "setup", 1);

  NVIM_MAP_CMD(L, "n", "<M-m>", "lua require('harpoon'):list():add()");
  NVIM_MAP_CMD(L, "n", "<leader>hm", "lua require('harpoon'):list():add()");
  NVIM_MAP_CMD(L, "n", "<M-l>", "lua require('harpoon').ui:toggle_quick_menu(require('harpoon'):list())");
  NVIM_MAP_CMD(L, "n", "<leader>hl", "lua require('harpoon').ui:toggle_quick_menu(require('harpoon'):list())");
  NVIM_MAP_CMD(L, "n", "<M-f>", "lua require('harpoon'):list():select(1)");
  NVIM_MAP_CMD(L, "n", "<leader>hf", "lua require('harpoon'):list():select(1)");
  NVIM_MAP_CMD(L, "n", "<M-d>", "lua require('harpoon'):list():select(2)");
  NVIM_MAP_CMD(L, "n", "<leader>hd", "lua require('harpoon'):list():select(2)");
  NVIM_MAP_CMD(L, "n", "<M-s>", "lua require('harpoon'):list():select(3)");
  NVIM_MAP_CMD(L, "n", "<leader>hs", "lua require('harpoon'):list():select(3)");
  NVIM_MAP_CMD(L, "n", "<M-a>", "lua require('harpoon'):list():select(4)");
  NVIM_MAP_CMD(L, "n", "<leader>ha", "lua require('harpoon'):list():select(4)");

  // lsp configuration presets
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/neovim/nvim-lspconfig"); }
  }

  // setup lsp
  {
    lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));

    // servers
    int lsp_servers_length = (int)STATIC_ARRAY_SIZE(g_lsp_servers);
    lua_getfield(L, -1, "lsp"); ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "enable");
    lua_createtable(L, lsp_servers_length, 0);
    {
      for(int i = 0;
          i < lsp_servers_length;
          i += 1)
      {
        MLUA_PUSH_IDX(L, i) { lua_pushstring(L, g_lsp_servers[i]); }
      }
    }
    MLUA_PCALL_VOID(L, 1);

    // features
    lua_getfield(L, -1, "diagnostic"); ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "config");
    lua_createtable(L, 0, 5);
    {
      MLUA_PUSH_KV(L, "signs") { lua_pushboolean(L, false); }
      MLUA_PUSH_KV(L, "underline") { lua_pushboolean(L, false); }
      MLUA_PUSH_KV(L, "update_in_insert") { lua_pushboolean(L, false); }
      MLUA_PUSH_KV(L, "virtual_text") { lua_pushboolean(L, false); }
      MLUA_PUSH_KV(L, "severity_sort") { lua_pushboolean(L, true); }
    }
    MLUA_PCALL_VOID(L, 1);

    // disable semantic highlights
    NVIM_MK_AUTOCMD_CALLBACK(
        L, "ColorScheme",
        "Disable LSP Highlights", "my-colorscheme-disable-lsp", false,
        lsp_disable_semantic_highlights);

    // on_attach
    NVIM_MK_AUTOCMD_CALLBACK(
        L, "LspAttach",
        "Setup LSP on the Buffer", "my-lsp-attach", true,
        lsp_on_attach);

    lua_pop(L, 1);
  }

#if MODE_FORMATTER
  // auto format
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/stevearc/conform.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "conform", 0, 2)
  {
    MLUA_PUSH_KV_TABLE(L, "formatters_by_ft", 0, 14)
    {
      MLUA_PUSH_KV_TABLE_IDX(L, "c") { lua_pushstring(L, "clang-format"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "cpp") { lua_pushstring(L, "clang-format"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "odin") { lua_pushstring(L, "odinfmt"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "rust") { lua_pushstring(L, "rustfmt"); }

      MLUA_PUSH_KV_TABLE(L, "haskell", 2, 1)
      {
        MLUA_PUSH_IDX(L, 1) { lua_pushstring(L, "fourmolu"); }
        MLUA_PUSH_IDX(L, 2) { lua_pushstring(L, "ormolu"); }
        MLUA_PUSH_KV(L, "stop_after_first") { lua_pushboolean(L, true); }
      }

      MLUA_PUSH_KV_TABLE_IDX(L, "clojure") { lua_pushstring(L, "cljfmt"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "java") { lua_pushstring(L, "google-java-format"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "cs") { lua_pushstring(L, "csharpier"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "lua") { lua_pushstring(L, "stylua"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "purescript") { lua_pushstring(L, "purescript-tidy"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "html") { lua_pushstring(L, "prettier"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "typescript") { lua_pushstring(L, "biome"); }
      MLUA_PUSH_KV_TABLE_IDX(L, "javascript") { lua_pushstring(L, "biome"); }
      MLUA_PUSH_KV(L, "python") { lua_pushcfunction(L, conform_formatters_by_ft_python); }
    }

    MLUA_PUSH_KV_TABLE_KV(L, "formatters", "odinfmt")
    {
      lua_createtable(L, 0, 3);
      {
        MLUA_PUSH_KV(L, "command") { lua_pushstring(L, "odinfmt"); }
        MLUA_PUSH_KV(L, "stdin") { lua_pushboolean(L, true); }

        MLUA_PUSH_KV_TABLE_IDX(L, "args") { lua_pushstring(L, "odinfmt"); }
      }
    }
  }

  NVIM_MAP_CMD(L, "n", "<leader>cf", "lua require('conform').format({ async = true, lsp_format = 'fallback' })");
#endif // MODE_FORMATTER

  // text semantics engine
  MLUA_MINIDEPS_ADD(L, 0, 2)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/nvim-treesitter/nvim-treesitter"); }

    MLUA_PUSH_KV_TABLE(L, "hooks", 0, 1)
    {
      MLUA_PUSH_KV(L, "post_checkout") { lua_pushcfunction(L, treesitter_update); }
    }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "nvim-treesitter", 0, 1)
  {
    MLUA_PUSH_KV(L, "auto_install") { lua_pushboolean(L, true); }
    MLUA_PUSH_KV_TABLE_KV(L, "highlight", "enable") { lua_pushboolean(L, false); }
    MLUA_PUSH_KV_TABLE_KV(L, "indent", "enable") { lua_pushboolean(L, false); }
  }

  // highlight special comments
  MLUA_MINIDEPS_ADD(L, 0, 2)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/folke/todo-comments.nvim"); }

    MLUA_PUSH_KV_TABLE_IDX(L, "depends") { lua_pushstring(L, "nvim-lua/plenary.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "todo-comments", 0, 3)
  {
    MLUA_PUSH_KV(L, "signs") { lua_pushboolean(L, false); }

    MLUA_PUSH_KV_TABLE_KV(L, "search", "pattern") { lua_pushstring(L, "\\b(KEYWORDS)(\\([^\\)]*\\))?:"); }

    MLUA_PUSH_KV_TABLE(L, "highlight", 0, 4)
    {
      MLUA_PUSH_KV(L, "before") { lua_pushstring(L, ""); }
      MLUA_PUSH_KV(L, "keyword") { lua_pushstring(L, "bg"); }
      MLUA_PUSH_KV(L, "after") { lua_pushstring(L, ""); }
      MLUA_PUSH_KV(L, "pattern") { lua_pushstring(L, ".*<((KEYWORDS)%(\\(.{-1,}\\))?):"); }
    }
  }

#if MODE_DESIGN
  // highlight color codes
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/catgoose/nvim-colorizer.lua"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "colorizer", 0, 1)
  {
    MLUA_PUSH_KV_TABLE_KV(L, "user_default_options", "names") { lua_pushboolean(L, false); }
  }
  NVIM_MAP_CMD(L, "n", "<leader>uh", "Colortils");

  // edit color codes
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/max397574/colortils.nvim"); }
  }

  MLUA_REQUIRE_SETUP_CALL(L, "colortils");
#endif // MODE_DESIGN

  // indent guides
  MLUA_MINIDEPS_ADD(L, 0, 1)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/lukas-reineke/indent-blankline.nvim"); }
  }

  MLUA_REQUIRE_SETUP_TABLE(L, "ibl", 0, 2)
  {
    MLUA_PUSH_KV_TABLE_KV(L, "scope", "enabled") { lua_pushboolean(L, false); }
    MLUA_PUSH_KV_TABLE_KV(L, "indent", "char") { lua_pushstring(L, "▏"); }
  }

#if MODE_THEME
  // install themes
  MLUA_MINIDEPS_ADD(L, 0, 2)
  {
    MLUA_PUSH_KV(L, "source") { lua_pushstring(L, "https://github.com/zenbones-theme/zenbones.nvim"); }
    MLUA_PUSH_KV_TABLE_IDX(L, "depends") { lua_pushstring(L, "rktjmp/lush.nvim"); }
  }

#if 0
  // other themes
  static char const *color_themes[] =
  {
    "https://github.com/EdenEast/nightfox.nvim",
    "https://github.com/ramojus/mellifluous.nvim",
    "https://github.com/rayes0/blossom.vim",
    "https://github.com/rebelot/kanagawa.nvim",
    "https://github.com/kepano/flexoki-neovim",
  };
  int color_themes_length = (int)STATIC_ARRAY_SIZE(color_themes);

  for(int i = 0;
      i < color_themes_length;
      i += 1)
  {
    MLUA_MINIDEPS_ADD(L, 0, 1)
    {
      MLUA_PUSH_KV(L, "source") { lua_pushstring(L, color_themes[i]); }
    }
  }

  MLUA_REQUIRE_SETUP_CALL(L, "flexoki");
  MLUA_REQUIRE_SETUP_CALL(L, "nightfox");
  MLUA_REQUIRE_SETUP_TABLE_CALL(L, "mellifluous");
#endif

  // enable theme
  lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "cmd"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "colorscheme");
  lua_pushstring(L, "rosebones");
  MLUA_PCALL_VOID(L, 1);
  lua_pop(L, 1);
#endif

  // theme type
  nvim_set_o(L, "background", nvim_mk_obj_string("light"));

  // highlights
  {
    Dict(highlight) hl = {0};
    PUT_KEY(hl, highlight, bg, nvim_mk_obj_string("#CFCFDA"));
    nvim_highlight(L, "ColorColumn", hl);
  }
  {
    Dict(highlight) hl = {0};
    PUT_KEY(hl, highlight, fg, nvim_mk_obj_string("#D0D1D8"));
    nvim_highlight(L, "Whitespace", hl);
  }

#if MODE_FOCUS
  // disable syntax highlighting
  lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "cmd"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "syntax");
  lua_pushstring(L, "off");
  MLUA_PCALL_VOID(L, 1);
  lua_pop(L, 1);
#endif

#if PERFORMANCE
  END_PERF_TIME(perf_times, Perf_Time_Download);

  END_PERF_TIME(perf_times, Perf_Time_Total);
  for(enum Perf_Time i = 0;
      i < (int)STATIC_ARRAY_SIZE(g_perf_time_strings);
      i += 1)
  {
    char out_buf[4096] = {0};
    snprintf(out_buf, sizeof(out_buf),
        "time took: %ld.%09ld; %s\n",
        perf_times[i][1].tv_sec - perf_times[i][0].tv_sec,
        perf_times[i][1].tv_nsec - perf_times[i][0].tv_nsec,
        g_perf_time_strings[i]);
    lua_getglobal(L, "vim");
    lua_getfield(L, -1, "print");
    lua_pushstring(L, out_buf);
    MLUA_PCALL_VOID(L, 1);
  }
#endif

#if DEBUG
  mlua_stack_dump_temp(L);
#endif

  /* EXIT */
  deinit_arena(&string_arena);
  return 0;
}
