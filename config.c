// TODO: replace any `do_cmdline_cmd` if possible
// TODO: test if the plugins/binds actually work
// TODO: refactor locality of option settings
// TODO: more asserts on lua types
// TODO: get ref for function keybinds

#include <ctype.h>
#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvim_api.c"

#include "config.h"
#include "arena.c"
#include "fileio.c"

#if DEBUG
#define PERFORMANCE 1
#endif

#if PERFORMANCE
static char const *g_perf_time_strings[] = {
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
  return (Object){
    .type = kObjectTypeString, .data.string = (String) {
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
  return (Object){
    .type = kObjectTypeString, .data.string = (String) {
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
nvim_mk_augroup_callback(
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

static inline Integer
nvim_mk_augroup_command(
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

// lua callers
#define LUA_PCALL(L, in, out) ASSERT(L, lua_pcall(L, in, out, 0) == 0)

#define LUA_PCALL_VOID(L, in, out) do { \
  LUA_PCALL(L, in, out); \
  lua_pop(L, 1); \
} while(0)

#define LUA_COLON(L, f) do { \
  lua_pushvalue(L, -1); \
  lua_getfield(L, -1, f); \
  lua_insert(L, -2); \
} while(0)

#define LUA_COLON_PCALL(L, f, in, out) do { \
  LUA_COLON(L, f); \
  LUA_PCALL(L, in, out); \
} while(0)

#define LUA_COLON_PCALL_VOID(L, f, in, out) do { \
  LUA_COLON_PCALL(L, f, in, out); \
  lua_pop(L, 1); \
} while(0)

// lua plugins
#define LUA_REQUIRE(L, name) do { \
  lua_getglobal(L, "require"); \
  lua_pushstring(L, name); \
  LUA_PCALL(L, 1, 1); \
  ASSERT(L, lua_istable(L, -1)); \
} while(0)

#define LUA_REQUIRE_SETUP(L, name) do { \
  LUA_REQUIRE(L, name); \
  lua_getfield(L, -1, "setup"); \
} while(0)

#define LUA_MINIDEPS_ADD(L) do { \
  lua_getglobal(L, "MiniDeps"); \
  ASSERT(L, lua_istable(L, -1)); \
  lua_getfield(L, -1, "add"); \
} while(0)

// lua kv
#define LUA_SET_KV(L) lua_settable(L, -3)

#define LUA_PUSH_KV(L, k, tv, v) do { \
  lua_pushstring(L, k); \
  lua_push##tv(L, v); \
  LUA_SET_KV(L); \
} while(0)

#define LUA_PUSH_KV_IDX(L, k, tv, v) do { \
  lua_pushstring(L, k); \
  lua_createtable(L, 1, 0); { LUA_PUSH_IDX(L, tv, v, 1); } \
  LUA_SET_KV(L); \
} while(0)

#define LUA_PUSH_KV_KV(L, k1, k2, tv, v) do { \
  lua_pushstring(L, k1); \
  lua_createtable(L, 1, 0); { LUA_PUSH_KV(L, k2, tv, v); } \
  LUA_SET_KV(L); \
} while(0)

// lua idx
#define LUA_SET_IDX(L, i) lua_rawseti(L, -2, i)

#define LUA_PUSH_IDX(L, tv, v, i) do { \
  lua_push##tv(L, v); \
  LUA_SET_IDX(L, i); \
} while(0)

// lua debug
static inline void
lua_stack_dump(
    lua_State *L)
{
  FILE *fptr = fopen(".stack_dump", "w"); // maybe find a better spot and/or take this as a parameter?
  int top = lua_gettop(L);
  for(int i = 1;
      i <= top;
      i++)
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
  fclose(fptr);
}

/* MAIN */
static char const *g_mini_comment_custom_commentstring_strings[][2] = {
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
      i++)
  {
    if(strcmp(ft, g_mini_comment_custom_commentstring_strings[i][0])
        == 0)
    {
      lua_pushlstring(
          L,
          g_mini_comment_custom_commentstring_strings[i][1],
          sizeof(g_mini_comment_custom_commentstring_strings[i][1]));
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
  lua_createtable(L, 0, 4); {
    LUA_PUSH_KV(L, "row", number, 0);
    LUA_PUSH_KV(L, "col", number, 0);
    LUA_PUSH_KV(L, "height", number, (float)lines.data.integer);
    LUA_PUSH_KV(L, "width", number, (float)columns.data.integer);
  }
  return 1;
}

int
conform_formatters_by_ft_python(
    lua_State *L)
{
  LUA_REQUIRE(L, "conform");
  lua_getfield(L, -1, "get_formatter_info");
    lua_pushstring(L, "ruff_format");
    lua_pushvalue(L, -4);
    LUA_PCALL(L, 2, 1);

    ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "available");

  ASSERT(L, lua_isboolean(L, -1));
  bool has_ruff = lua_toboolean(L, -1);
  lua_pop(L, 4);

  lua_createtable(L, 2, 0);
  if(has_ruff)
  {
    LUA_PUSH_IDX(L, string, "ruff_format", 1);
  }
  else
  {
    LUA_PUSH_IDX(L, string, "isort", 1);
    LUA_PUSH_IDX(L, string, "black", 2);
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
  LUA_PCALL(L, 1, 1);

  LUA_COLON(L, "supports_method");
  lua_pushstring(L,"textDocument/completion");
  LUA_PCALL(L, 2, 1);
  if(lua_toboolean(L, -1))
  {
    lua_getfield(L, 4, "completion"); ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "enable");
    lua_pushboolean(L, true);
    lua_getfield(L, 5, "id");
    lua_pushvalue(L, 2);
    LUA_PCALL_VOID(L, 3, 0);
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
  LUA_PCALL(L, 2, 1);

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
  LUA_REQUIRE_SETUP(L, "mini.deps");
  lua_createtable(L, 1, 0); {
    lua_pushstring(L, "path");
    lua_createtable(L, 0, 1); {
      lua_pushstring(L, "package");
      lua_pushlstring(L, plugin_path, plugin_path_len);
      LUA_SET_KV(L);
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

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
  NVIM_MAP_CMD(L, "n", "<leader>cW", "s/\\s\\+$//g"); // remove trailing whitespace

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
  nvim_mk_augroup_command(L, "TextYankPost", "Highlight when yanking text", "my-highlight-yank", true,
      nvim_mk_string("lua vim.highlight.on_yank({ on_visual = false })"));

#if PERFORMANCE
  END_PERF_TIME(perf_times, Perf_Time_Opt);

  perf_times[Perf_Time_Download][0] = perf_times[Perf_Time_Opt][1];
#endif

  /* Download Packages */
  // mini
  LUA_REQUIRE_SETUP(L, "mini.ai");
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "n_lines", integer, 500);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "mini.bracketed");
  LUA_PCALL_VOID(L, 0, 0);

  LUA_REQUIRE_SETUP(L, "mini.comment");
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "ignore_blank_line", boolean, true);

    lua_pushstring(L, "options");
    lua_createtable(L, 0, 1); {
      LUA_PUSH_KV(L, "custom_commentstring", cfunction, mini_comment_custom_commentstring);
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Detect Tabbing
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/tpope/vim-sleuth");
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Git Signs
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/lewis6991/gitsigns.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "gitsigns");
  lua_createtable(L, 0, 1); {
    lua_pushstring(L, "signs");
    lua_createtable(L, 0, 5); {
      LUA_PUSH_KV_KV(L, "add", "text", string, "+");
      LUA_PUSH_KV_KV(L, "change", "text", string, "~");
      LUA_PUSH_KV_KV(L, "delete", "text", string, "_");
      LUA_PUSH_KV_KV(L, "topdelete", "text", string, "‾");
      LUA_PUSH_KV_KV(L, "changedelete", "text", string, "~");
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Git Signs Hunks
  NVIM_MAP_CMD(L, "n", "]h", "lua require('gitsigns').nav_hunk('next')");
  NVIM_MAP_CMD(L, "n", "[h", "lua require('gitsigns').nav_hunk('prev')");

  // File Explorer
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/stevearc/oil.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "oil");
  lua_createtable(L, 0, 5); {
    lua_pushstring(L, "columns");
    lua_createtable(L, 2, 0); {
      char *nvim_oil_columns[] = {
        // "permissions",
        // "size",
        "mtime",
        "icon",
      };
      for(int i = 0;
          i < (int)STATIC_ARRAY_SIZE(nvim_oil_columns);
          i++)
      {
        LUA_PUSH_IDX(L, string, nvim_oil_columns[i], i + 1);
      }
    }
    LUA_SET_KV(L);

    LUA_PUSH_KV(L, "natural_order", boolean, true);
    LUA_PUSH_KV(L, "delete_to_trash", boolean, true);

    LUA_PUSH_KV_KV(L, "view_options", "show_hidden", boolean, true);

    lua_pushstring(L, "keymaps");
    lua_createtable(L, 0, 8); {
      LUA_PUSH_KV(L, "g?", string, "actions.show_help");
      LUA_PUSH_KV(L, "<CR>", string, "actions.select");
      LUA_PUSH_KV(L, "L", string, "actions.select");
      LUA_PUSH_KV(L, "H", string, "actions.parent");
      LUA_PUSH_KV(L, "<C-c>", string, "actions.close");
      LUA_PUSH_KV(L, "<C-l>", string, "actions.refresh");
      LUA_PUSH_KV(L, "g.", string, "actions.toggle_hidden");
      LUA_PUSH_KV(L, "g\\", string, "actions.toggle_trash");
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  NVIM_MAP_CMD(L, "n", "<leader>uf", "Oil");

  // Visual Undo Tree
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/jiaoshijie/undotree");
    LUA_PUSH_KV_IDX(L, "depends", string, "nvim-lua/plenary.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "undotree");
  LUA_PCALL_VOID(L, 0, 0);
  NVIM_MAP_CMD(L, "n", "<leader>cu", "lua require('undotree').toggle()");

  // Show Keybinds
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/folke/which-key.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "which-key");
  lua_createtable(L, 0, 2); {
    LUA_PUSH_KV(L, "delay", integer, 300);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Search Engine
  LUA_REQUIRE_SETUP(L, "mini.pick");
  lua_createtable(L, 0, 1); {
    lua_pushstring(L, "window");
    lua_createtable(L, 0, 1); {
      LUA_PUSH_KV(L, "config", cfunction, mini_pick_window_config);
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  NVIM_MAP_CMD(L, "n", "<leader>sf", "Pick files");
  NVIM_MAP_CMD(L, "n", "<leader>sg", "Pick grep_live");
  NVIM_MAP_CMD(L, "n", "<leader>so", "Pick buffers");
  NVIM_MAP_CMD(L, "n", "<leader>sn",
      "lua MiniPick.start({ source = { cwd = vim.fn.stdpath('config') } }))");
  NVIM_MAP_CMD(L, "n", "<leader>sm",
      "lua MiniPick.start({ source = { items = "
      "vim.fn.systemlist('man -k ' .. vim.fn.input('Man page: ')) } })");

  // QOL Improvements for marks
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/chentoast/marks.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "marks");
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "default_mappings", boolean, true);

    lua_pushstring(L, "mappings");
    lua_createtable(L, 0, 0);
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Jump To Files
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 3); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/ThePrimeagen/harpoon");
    LUA_PUSH_KV(L, "checkout", string, "harpoon2");

    LUA_PUSH_KV_IDX(L, "depends", string, "nvim-lua/plenary.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE(L, "harpoon");
  LUA_COLON_PCALL_VOID(L, "setup", 1, 0);

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

  // LSP Configuration Presets
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/neovim/nvim-lspconfig");
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Setup LSP
  {
    lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));

    // servers
    char *lsp_servers[] = {
      "lua_ls",
      "clangd",
      "ols",
    };
    lua_getfield(L, -1, "lsp"); ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "enable");
    lua_createtable(L, 3, 0); {
      for(int i = 0;
          i < (int)STATIC_ARRAY_SIZE(lsp_servers);
          i++)
      {
        LUA_PUSH_IDX(L, string, lsp_servers[i], i);
      }
    }
    LUA_PCALL_VOID(L, 1, 0);

    // features
    lua_getfield(L, -1, "diagnostic"); ASSERT(L, lua_istable(L, -1));
    lua_getfield(L, -1, "config");
    lua_createtable(L, 0, 5); {
      LUA_PUSH_KV(L, "signs", boolean, false);
      LUA_PUSH_KV(L, "underline", boolean, false);
      LUA_PUSH_KV(L, "update_in_insert", boolean, false);
      LUA_PUSH_KV(L, "virtual_text", boolean, false);
      LUA_PUSH_KV(L, "severity_sort", boolean, true);
    }
    LUA_PCALL_VOID(L, 1, 0);

    // disable semantic highlights
    lsp_disable_semantic_highlights(L);
    lua_register(L, "g_lsp_disable_semantic_highlights", lsp_disable_semantic_highlights);
    lua_getglobal(L, "g_lsp_disable_semantic_highlights");
    int lua_ref_lsp_disable_semantic_highlights = luaL_ref(L, LUA_REGISTRYINDEX);
    nvim_mk_augroup_callback(L, "ColorScheme", "Disable LSP Highlights", "my-colorscheme-disable-lsp", false,
        nvim_mk_obj_luaref(lua_ref_lsp_disable_semantic_highlights));

    // on_attach
    lua_register(L, "g_lsp_on_attach", lsp_on_attach);
    lua_getglobal(L, "g_lsp_on_attach");
    int lua_ref_lsp_on_attach = luaL_ref(L, LUA_REGISTRYINDEX);
    nvim_mk_augroup_callback(L, "LspAttach", "Setup LSP on the Buffer", "my-lsp-attach", true,
        nvim_mk_obj_luaref(lua_ref_lsp_on_attach));

    lua_pop(L, 1);
  }

#if MODE_FORMATTER
  // Auto Format
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/stevearc/conform.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "conform");
  lua_createtable(L, 0, 2); {
    lua_pushstring(L, "formatters_by_ft");
    lua_createtable(L, 0, 14); {
      LUA_PUSH_KV_IDX(L, "c", string, "clang-format");
      LUA_PUSH_KV_IDX(L, "cpp", string, "clang-format");
      LUA_PUSH_KV_IDX(L, "odin", string, "odinfmt");
      LUA_PUSH_KV_IDX(L, "rust", string, "rustfmt");

      lua_pushstring(L, "haskell");
      lua_createtable(L, 2, 1); {
        LUA_PUSH_IDX(L, string, "fourmolu", 1);
        LUA_PUSH_IDX(L, string, "ormolu", 2);
        LUA_PUSH_KV(L, "stop_after_first", boolean, true);
      }
      LUA_SET_KV(L);

      LUA_PUSH_KV_IDX(L, "clojure", string, "cljfmt");
      LUA_PUSH_KV_IDX(L, "java", string, "google-java-format");
      LUA_PUSH_KV_IDX(L, "cs", string, "csharpier");
      LUA_PUSH_KV_IDX(L, "lua", string, "stylua");
      LUA_PUSH_KV_IDX(L, "purescript", string, "purescript-tidy");
      LUA_PUSH_KV_IDX(L, "html", string, "prettier");
      LUA_PUSH_KV_IDX(L, "typescript", string, "biome");
      LUA_PUSH_KV_IDX(L, "javascript", string, "biome");
      LUA_PUSH_KV(L, "python", cfunction, conform_formatters_by_ft_python);
    }
    LUA_SET_KV(L);

    lua_pushstring(L, "formatters");
    lua_createtable(L, 0, 1); {
      lua_pushstring(L, "odinfmt");
      lua_createtable(L, 0, 1); {
        LUA_PUSH_KV(L, "command", string, "odinfmt");
        LUA_PUSH_KV(L, "stdin", boolean, true);

        lua_pushstring(L, "args");
        lua_createtable(L, 1, 0); { LUA_PUSH_IDX(L, string, "odinfmt", 1); }
        LUA_SET_KV(L);
      }
      LUA_SET_KV(L);
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  NVIM_MAP_CMD(L, "n", "<leader>cf", "lua require('conform').format({ async = true, lsp_format = 'fallback' })");
#endif // MODE_FORMATTER

  // Text Semantics Engine
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/nvim-treesitter/nvim-treesitter");

    lua_pushstring(L, "hooks");
    lua_createtable(L, 0, 1); {
      // TODO: LUA_PUSH_KV(L, "post_checkout", string, "function() vim.cmd('TSUpdate') end");
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "nvim-treesitter");
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "auto_install", boolean, true);
    LUA_PUSH_KV_KV(L, "highlight", "enable", boolean, false);
    LUA_PUSH_KV_KV(L, "indent", "enable", boolean, false);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Highlight Special Comments
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/folke/todo-comments.nvim");

    LUA_PUSH_KV_IDX(L, "depends", string, "nvim-lua/plenary.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "todo-comments");
  lua_createtable(L, 0, 3); {
    LUA_PUSH_KV(L, "signs", boolean, false);

    LUA_PUSH_KV_KV(L, "search", "pattern", string, "\\b(KEYWORDS)(\\([^\\)]*\\))?:");

    lua_pushstring(L, "highlight");
    lua_createtable(L, 0, 1); {
      LUA_PUSH_KV(L, "before", string, "");
      LUA_PUSH_KV(L, "keyword", string, "bg");
      LUA_PUSH_KV(L, "after", string, "");
      LUA_PUSH_KV(L, "pattern", string, ".*<((KEYWORDS)%(\\(.{-1,}\\))?):");
    }
    LUA_SET_KV(L);
  }
  LUA_PCALL_VOID(L, 1, 0);

#if MODE_DESIGN
  // Highlight Color Codes
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/catgoose/nvim-colorizer.lua");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "colorizer");
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV_KV(L, "user_default_options", "names", boolean, false);
  }
  LUA_PCALL_VOID(L, 1, 0);
  NVIM_MAP_CMD(L, "n", "<leader>uh", "Colortils");

  // Edit Color Codes
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/max397574/colortils.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "colortils");
  LUA_PCALL_VOID(L, 0, 0);
#endif // MODE_DESIGN

  // Theme
#if MODE_THEME
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_PUSH_KV(L, "source", string, "https://github.com/zenbones-theme/zenbones.nvim");

    LUA_PUSH_KV_IDX(L, "depends", string, "rktjmp/lush.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  // TODO: nvim_set_g("tokyobones", = {
  //   lightness = 'bright',
  //   transparent_background = true,
  //   darken_comments = 45,
  // });

  lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "cmd"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "colorscheme");
  lua_pushstring(L, "tokyobones");
  LUA_PCALL_VOID(L, 1, 0);
  lua_pop(L, 1);
#endif // MODE_THEME

  // highlights
  {
    Dict(highlight) hl = {0};
    PUT_KEY(hl, highlight, bg, nvim_mk_obj_string("#C7CBDB"));
    nvim_highlight(L, "ColorColumn", hl);
  }
  {
    Dict(highlight) hl = {0};
    PUT_KEY(hl, highlight, fg, nvim_mk_obj_string("#D0D1D8"));
    nvim_highlight(L, "Whitespace", hl);
  }

  /* // disable syntax highlighting
  lua_getglobal(L, "vim"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "cmd"); ASSERT(L, lua_istable(L, -1));
  lua_getfield(L, -1, "syntax");
  lua_pushstring(L, "off");
  LUA_PCALL_VOID(L, 1, 0);
  lua_pop(L, 1);
  */

  // Themeing
  nvim_set_o(L, "background", nvim_mk_obj_string("light"));

#if PERFORMANCE
  END_PERF_TIME(perf_times, Perf_Time_Download);

  END_PERF_TIME(perf_times, Perf_Time_Total);
  for(enum Perf_Time i = 0;
      i < (int)STATIC_ARRAY_SIZE(g_perf_time_strings);
      i++)
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
    LUA_PCALL_VOID(L, 1, 0);
  }

  lua_stack_dump(L);
#endif

  /* EXIT */
  deinit_arena(&string_arena);
  return 0;
}
