// TODO: reduce the size of lua
// TODO: replace any `do_cmdline_cmd` if possible
// TODO: replace any `lua MiniDeps.add` if possible
// TODO: replace any `lua require` if possible
// TODO: test if the plugins/binds actually work

#include <ctype.h>
#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvim_api.c"

#include "config.h"
#include "arena.c"
#include "fileio.c"

/* GLOBALS */
static char const g_package_dir[] = "/site/";
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
  Dict(option) o = {};
  Error e = ERROR_INIT;
  nvim_set_option_value(0, nvim_mk_string(key), val, &o, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

// Key Mapping
static inline void
nvim_map(
    lua_State *L,
    char *mode,
    char *key,
    char *action)
{
  Dict(keymap) o = {.noremap = true, .silent = true};
  Error e = ERROR_INIT;
  nvim_set_keymap(0, nvim_mk_string(mode), nvim_mk_string(key), nvim_mk_string(action), &o, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
}

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
static inline Integer // WARN: untested
nvim_mk_augroup_callback(
    lua_State *L,
    char *name,
    char *desc,
    char *augroup_name,
    bool augroup_clear,
    Union(String, LuaRefOf((DictAs(create_autocmd__callback_args) args), *Boolean)) callback)
{
  Arena arena = ARENA_EMPTY; // WARN: I dont know if this lives long enough
  Error e = ERROR_INIT;

  Dict(create_augroup) augroup = {};
  PUT_KEY(augroup, create_augroup, clear, augroup_clear);

  Dict(create_autocmd) autocmd = {};
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
  Arena arena = ARENA_EMPTY; // WARN: I dont know if this lives long enough
  Error e = ERROR_INIT;

  Dict(create_augroup) augroup = {};
  PUT_KEY(augroup, create_augroup, clear, augroup_clear);

  Dict(create_autocmd) autocmd = {};
  PUT_KEY(autocmd, create_autocmd, desc, nvim_mk_string(desc));
  PUT_KEY(autocmd, create_autocmd, group,
      nvim_mk_obj_int(nvim_create_augroup(0, nvim_mk_string(augroup_name), &augroup, &e)));
  PUT_KEY(autocmd, create_autocmd, command, command);

  Integer n = nvim_create_autocmd(0, nvim_mk_obj_string(name), &autocmd, &arena, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR(%d): %s\n", e.type, e.msg); }
  return n;
}

// lua helpers
#define LUA_PCALL(L, in, out) ASSERT(L, lua_pcall(L, in, out, 0) == 0)

#define LUA_PCALL_VOID(L, in, out) do { \
  LUA_PCALL(L, in, out); \
  lua_pop(L, 1); \
} while(0)

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

#define LUA_KV_SET_STR(L, k, v) do { \
  lua_pushstring(L, k); \
  lua_pushstring(L, v); \
  lua_settable(L, -3); \
} while(0)

#define LUA_KV_SET_TYPE(L, tk, k, tv, v) do { \
  lua_push##tk(L, k); \
  lua_push##tv(L, v); \
  lua_settable(L, -3); \
} while(0)

static inline void
lua_stack_dump(
    lua_State *L)
{
  FILE *fptr = fopen(".stack_dump", "w");
  int top = lua_gettop(L);
  for(int i = 1;
      i <= top;
      i++)
  {
    fprintf(fptr, "%d\t%s\t", i, luaL_typename(L, i));
    switch (lua_type(L, i))
    {
    case LUA_TNUMBER: { fprintf(fptr, "%g\n",lua_tonumber(L, i)); } break;
    case LUA_TSTRING: { fprintf(fptr, "%s\n",lua_tostring(L, i)); } break;
    case LUA_TBOOLEAN: { fprintf(fptr, "%s\n", (lua_toboolean(L, i) ? "true" : "false")); } break;
    case LUA_TNIL: { fprintf(fptr, "%s\n", "nil"); } break;
    default: { fprintf(fptr, "%p\n",lua_topointer(L, i)); } break;
    }
  }
  fclose(fptr);
}

// WARN: only works with utf-8, better than only ascii right??
Codepoint
try_next_codepoint(
    uint8_t **string,
    size_t *len)
{
  if(*len == 0) { return CODEPOINT_INVALID; }

  Codepoint codepoint = CODEPOINT_INVALID;
  uint8_t *str = *string;

  if(str[0] <= 0x7F) // ascii
  {
    codepoint = str[0];
    *string += 1;
    *len -= 1;
  }
  else if((str[0] >> 5) == 0x6) // 2
  {
    // check valid [1]
    if(*len < 2) { goto EXIT; }
    codepoint =
        ((str[0] & 0x1F) << 6) |
        (str[1] & 0x3F);
    *string += 2;
    *len -= 2;
  }
  else if((str[0] >> 4) == 0xE) // 3
  {
    if(*len < 3) { goto EXIT; }
    codepoint =
        ((str[0] & 0xF) << 12) |
        ((str[1] & 0x3F) << 6) |
        (str[2] & 0x3F);
    *string += 3;
    *len -= 3;
  }
  else if((str[0] >> 3) == 0x1E) // 4
  {
    if(*len < 4) { goto EXIT; }
    codepoint =
        ((str[0] & 0x7) << 18) |
        ((str[1] & 0x3F) << 12) |
        ((str[2] & 0x3F) << 6) |
        (str[3] & 0x3F);
    *string += 4;
    *len -= 4;
  }

EXIT:
  return codepoint;
}

/* MAIN */
int
completer(
    lua_State *L)
{
  int nargs = lua_gettop(L);
  if(nargs < 1)
  {
    PANIC(L, "completer: expected at least 1 arg");
    return 1;
  }

  int findstart = (int)lua_tointeger(L, 1);

  Arena arena = ARENA_EMPTY;
  Error err = ERROR_INIT;

  if(findstart == 1)
  {
    do_cmdline_cmd("lua print(vim.inspect(vim.api.nvim_buf_get_lines(0, 0, -1, false)))");
    do_cmdline_cmd("lua print(vim.in_fast_event())");
    // get line
    String curline = nvim_get_current_line(&arena, &err);
    if(curline.size == 0) { goto EXIT; }

    // get current pos in line
    ArrayOf(Integer, 2) cursor = nvim_win_get_cursor(0, &arena, &err);
    int col = (int)cursor.items[1].data.integer - 2;
    ASSERT(L, col >= 0);
    ASSERT(L, (size_t)col < curline.size);

    // search backwards for start of identifier
    size_t i;
    for(i = (size_t)col;
        i > 0;
        i--)
    {
      char ch = curline.data[i - 1];
      if(!(isalpha(ch) || isdigit(ch) || ch == '_')) // TODO: depends on language normally :)
      {
        break;
      }
    }

    // return start of ident
    lua_pushinteger(L, i);
    return 1;
  }
  else // longest match
  {
    do_cmdline_cmd("lua print(vim.inspect(vim.api.nvim_buf_get_lines(0, 0, -1, false)))");
    do_cmdline_cmd("lua print(vim.in_fast_event())");
    size_t base_len = 0;
    char const *base = NULL;
    if(nargs >= 2 && lua_type(L, 2) == LUA_TSTRING)
    {
      base = lua_tolstring(L, 2, &base_len);
    }
    else
    {
      base_len = 0;
      base = NULL;
    }
    (void)base;
    if(base_len == 0) { goto EXIT; }

    // TODO: get buftype

    // TODO: table of matches

    // longest
    {
      // each buffer
      // PERF: maybe worker thread this?
      ArrayOf(Buffer) bufs = nvim_list_bufs(&arena);
      for(size_t bi = 0;
          bi < bufs.size;
          bi++)
      {
        Buffer buf = bufs.items[bi].data.integer;
        if(!nvim_buf_is_loaded(buf)) { continue; }

        // each line
        // TODO: COMPLETER DOES NOT WORK, CANT GET THE LINES FOR SOME REASON
        ArrayOf(String) lines = nvim_buf_get_lines(0, buf, 0, -1, false, &arena, L, &err);
        ASSERT(L, lines.size > 0);
        for(size_t li = 0;
            li < lines.size;
            li++)
        {
          // find each word in the line
          String curr_line = lines.items[li].data.string;
          if(curr_line.size == 0) { continue; }
          Codepoint curr_codepoint = curr_line.data[0];
          while(curr_line.size != 0)
          {
            // skip non identifiers
            while(!(isalpha(curr_codepoint) || curr_codepoint == '_'))
            {
              curr_codepoint = try_next_codepoint((uint8_t **)&curr_line.data, &curr_line.size);
            }

            char *ident = curr_line.data;
            // find end of identifier
            while(isalpha(curr_codepoint)
                || isdigit(curr_codepoint)
                || curr_codepoint == '_')
            {
              curr_codepoint = try_next_codepoint((uint8_t **)&curr_line.data, &curr_line.size);
            }
            size_t ident_len = curr_line.data - ident;

            // check if the word is prefixed with the base
            PANIC_FMT(L, "%*s\n", ident_len, ident);
            if(base_len <= ident_len && memcmp(base, ident, base_len))
            {
              PANIC_FMT(L, "compare remainder with the longest match `%*s` `%*s`\n",
                  base_len, base, ident_len, ident);
            }
          }
        }
      }
    }

    lua_createtable(L, 1, 0);
    lua_pushstring(L, "nonsense");
    lua_rawseti(L, -2, 1);
    return 1;
  }

EXIT:
  return 0;
}

int
luaopen_config(
    lua_State *L)
{
  // setup command string creator, useful for temporary strings
  struct Arena string_arena;
  ASSERT(L, init_arena(&string_arena, 4096 * 4)); // arbitrary size

  /* RUNTIME */
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

  // try installing mini.deps
  {
    uint reset_delta_len = string_arena.length; // for later reset

    // setup plugin_path for mini.nvim
    ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)g_mini_plugin_dir, sizeof(g_mini_plugin_dir) - 1));
    reset_delta_len -= string_arena.length;

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

    // remove mini.nvim from plugin_path
    string_arena.length += reset_delta_len;
    string_arena.buffer[string_arena.length] = 0;
  }

  /* PRE OPTIONS */
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
  do_cmdline_cmd("set cinoptions+=:0,l1");

  // completion
  nvim_set_o(L, "wildmode", nvim_mk_obj_string("longest:full"));
  nvim_set_o(L, "wildmenu", nvim_mk_obj_bool(true));

  nvim_set_o(L, "completeopt", nvim_mk_obj_string("longest"));
  nvim_set_o(L, "completefunc", nvim_mk_obj_string("v:lua.completer"));

  /* SETUP THE PACKAGE MANAGER */
  // require the package manager
  do_cmdline_cmd("packadd mini.nvim | helptags ALL");
  {
    LUA_REQUIRE_SETUP(L, "mini.deps");
    lua_createtable(L, 1, 0); {
      lua_pushstring(L, "path");
      lua_createtable(L, 0, 1); {
        lua_pushstring(L, "package");
        lua_pushlstring(L, plugin_path, plugin_path_len);
        lua_settable(L, -3);
      }
      lua_settable(L, -3);
    }
    LUA_PCALL_VOID(L, 1, 0);
  }

  /* download stuff */
  // mini
  LUA_REQUIRE_SETUP(L, "mini.ai");
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_TYPE(L, string, "n_lines", integer, 500);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "mini.bracketed");
  LUA_PCALL_VOID(L, 0, 0);

  do_cmdline_cmd(
  "lua require('mini.comment').setup {\n"
    "options = {\n"
      "custom_commentstring = function()\n"
        "local remap = {\n"
          "verilog = '/* %s */',\n"
        "}\n"
        "for ft, cm in pairs(remap) do\n"
          "if vim.bo.ft == ft then\n"
            "return cm\n"
          "end\n"
        "end\n"
        "return nil\n"
      "end,\n"
      "ignore_blank_line = true,\n"
    "},\n"
  "}");

  // Detect Tabbing
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "tpope/vim-sleuth");
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Git Signs
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "lewis6991/gitsigns.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "gitsigns");
  lua_createtable(L, 0, 1); {
    lua_pushstring(L, "signs");
    lua_createtable(L, 0, 5); {
      lua_pushstring(L, "add");
      lua_createtable(L, 0, 1); {
        LUA_KV_SET_STR(L, "text", "+");
      }
      lua_settable(L, -3);

      lua_pushstring(L, "change");
      lua_createtable(L, 0, 1); {
        LUA_KV_SET_STR(L, "text", "~");
      }
      lua_settable(L, -3);

      lua_pushstring(L, "delete");
      lua_createtable(L, 0, 1); {
        LUA_KV_SET_STR(L, "text", "_");
      }
      lua_settable(L, -3);

      lua_pushstring(L, "topdelete");
      lua_createtable(L, 0, 1); {
        LUA_KV_SET_STR(L, "text", "‾");
      }
      lua_settable(L, -3);

      lua_pushstring(L, "changedelete");
      lua_createtable(L, 0, 1); {
        LUA_KV_SET_STR(L, "text", "~");
      }
      lua_settable(L, -3);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Git Signs Hunks
  nvim_map(L, "n", "]h", "<cmd>lua require('gitsigns').nav_hunk('next')<cr>");
  nvim_map(L, "n", "[h", "<cmd>lua require('gitsigns').nav_hunk('prev')<cr>");

  // File Explorer
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "stevearc/oil.nvim");
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
        lua_pushstring(L, nvim_oil_columns[i]);
        lua_rawseti(L, -2, i + 1);
      }
    }
    lua_settable(L, -3);

    LUA_KV_SET_TYPE(L, string, "natural_order", boolean, true);
    LUA_KV_SET_TYPE(L, string, "delete_to_trash", boolean, true);

    lua_pushstring(L, "view_options");
    lua_createtable(L, 0, 1); {
      LUA_KV_SET_TYPE(L, string, "icon", boolean, true);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "keymaps");
    lua_createtable(L, 0, 8); {
      LUA_KV_SET_STR(L, "g?", "actions.show_help");
      LUA_KV_SET_STR(L, "<CR>", "actions.select");
      LUA_KV_SET_STR(L, "L", "actions.select");
      LUA_KV_SET_STR(L, "H", "actions.parent");
      LUA_KV_SET_STR(L, "<C-c>", "actions.close");
      LUA_KV_SET_STR(L, "<C-l>", "actions.refresh");
      LUA_KV_SET_STR(L, "g.", "actions.toggle_hidden");
      LUA_KV_SET_STR(L, "g\\", "actions.toggle_trash");
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  nvim_map(L, "n", "<leader>uf", "<cmd>Oil<cr>");

  // Visual Undo Tree
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_STR(L, "source", "jiaoshijie/undotree");
    lua_pushstring(L, "depends");
    lua_createtable(L, 1, 0); {
      lua_pushstring(L, "nvim-lua/plenary.nvim");
      lua_rawseti(L, -2, 1);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "undotree");
  LUA_PCALL_VOID(L, 0, 0);
  nvim_map(L, "n", "<leader>cu", "<cmd>lua require('undotree').toggle()<cr>");

  // Show Keybinds
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_STR(L, "source", "folke/which-key.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "which-key");
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_TYPE(L, string, "delay", integer, 300);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_STR(L, "source", "nvim-telescope/telescope.nvim");

    lua_pushstring(L, "depends");
    lua_createtable(L, 2, 0); {
      lua_createtable(L, 0, 1); {
        LUA_KV_SET_STR(L, "source", "nvim-lua/plenary.nvim");
      }
      lua_rawseti(L, -2, 1);

      lua_createtable(L, 0, 3); {
        LUA_KV_SET_STR(L, "source", "nvim-telescope/telescope-fzf-native.nvim");
        LUA_KV_SET_STR(L, "build", "make");
        LUA_KV_SET_STR(L, "cond", "function() return vim.fn.executable 'make' == 1 end"); // WARN: idk if that worked
      }
      lua_rawseti(L, -2, 2);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "telescope"); LUA_PCALL_VOID(L, 0, 0);
  LUA_REQUIRE_SETUP(L, "telescope"); lua_pushstring(L, "fzf"); LUA_PCALL_VOID(L, 1, 0);
  LUA_REQUIRE_SETUP(L, "telescope"); lua_pushstring(L, "ui-select"); LUA_PCALL_VOID(L, 1, 0);
  nvim_map(L, "n", "<leader>sn", "<cmd>lua require('telescope.builtin').find_files({cwd=vim.fn.stdpath('config')})<cr>");
  nvim_map(L, "n", "<leader>sf", "<cmd>lua require('telescope.builtin').find_files()<cr>");
  nvim_map(L, "n", "<leader>sg", "<cmd>lua require('telescope.builtin').live_grep()<cr>");
  nvim_map(L, "n", "<leader>sd", "<cmd>lua require('telescope.builtin').diagnostics()<cr>");
  nvim_map(L, "n", "<leader>s.", "<cmd>lua require('telescope.builtin').oldfiles()<cr>");
  nvim_map(L, "n", "<leader>so", "<cmd>lua require('telescope.builtin').buffers()<cr>");
  nvim_map(L, "n", "<leader>sm", "<cmd>lua require('telescope.builtin').man_pages({sections={'ALL'}})<cr>");

  // QOL Improvements for marks
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "chentoast/marks.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "marks");
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_TYPE(L, string, "default_mappings", boolean, true);

    lua_pushstring(L, "mappings");
    lua_createtable(L, 0, 0);
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  // Jump To Files
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 3); {
    LUA_KV_SET_STR(L, "source", "ThePrimeagen/harpoon");
    LUA_KV_SET_STR(L, "checkout", "harpoon2");

    lua_pushstring(L, "depends");
    lua_createtable(L, 1, 0); {
      lua_pushstring(L, "nvim-lua/plenary.nvim");
      lua_rawseti(L, -2, 1);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL(L, 1, 0);

  LUA_REQUIRE(L, "harpoon");
  lua_pushvalue(L, -1);
  lua_getfield(L, -1, "setup");
  lua_insert(L, -2);
  LUA_PCALL_VOID(L, 1, 0);

  nvim_map(L, "n", "<M-m>", "<cmd>lua require('harpoon'):list():add()<cr>");
  nvim_map(L, "n", "<leader>hm", "<cmd>lua require('harpoon'):list():add()<cr>");
  nvim_map(L, "n", "<M-l>", "<cmd>lua require('harpoon').ui:toggle_quick_menu(require('harpoon'):list())<cr>");
  nvim_map(L, "n", "<leader>hl", "<cmd>lua require('harpoon').ui:toggle_quick_menu(require('harpoon'):list())<cr>");
  nvim_map(L, "n", "<M-f>", "<cmd>lua require('harpoon'):list():select(1)<cr>");
  nvim_map(L, "n", "<leader>hf", "<cmd>lua require('harpoon'):list():select(1)<cr>");
  nvim_map(L, "n", "<M-d>", "<cmd>lua require('harpoon'):list():select(2)<cr>");
  nvim_map(L, "n", "<leader>hd", "<cmd>lua require('harpoon'):list():select(2)<cr>");
  nvim_map(L, "n", "<M-s>", "<cmd>lua require('harpoon'):list():select(3)<cr>");
  nvim_map(L, "n", "<leader>hs", "<cmd>lua require('harpoon'):list():select(3)<cr>");
  nvim_map(L, "n", "<M-a>", "<cmd>lua require('harpoon'):list():select(4)<cr>");
  nvim_map(L, "n", "<leader>ha", "<cmd>lua require('harpoon'):list():select(4)<cr>");

  // SUGGEST: do we want to support LSP Config?

  // Auto Format
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "stevearc/conform.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);
do_cmdline_cmd("lua require('conform').setup { \
  formatters_by_ft = { \
    c = { 'clang-format' }, \
    cpp = { 'clang-format' }, \
    clojure = { 'cljfmt' }, \
    haskell = { 'fourmolu', 'ormolu', stop_after_first = true }, \
    html = { 'prettier' }, \
    java = { 'google-java-format' }, \
    lua = { 'stylua' }, \
    odin = { 'odinfmt' }, \
    purescript = { 'purescript-tidy' }, \
    python = function(bufnr) \
      if require('conform').get_formatter_info('ruff_format', bufnr).available then \
        return { 'ruff_format' } \
      else \
        return { 'isort', 'black' } \
      end \
    end, \
    rust = { 'rustfmt' }, \
    cs = { 'csharpier' }, \
    typescript = { 'biome' }, \
    javascript = { 'biome' }, \
  }, \
  formatters = { \
    odinfmt = { \
      --[[ Change where to find the command if it isn't in your path.]] \
      command = 'odinfmt', \
      args = { '-stdin' }, \
      stdin = true, \
    }, \
  }, \
}");
  nvim_map(L, "n", "<leader>cf", "<cmd>require('conform').format({ async = true, lsp_format = 'fallback' })<cr>");

// Text Semantics Engine
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_STR(L, "source", "nvim-treesitter/nvim-treesitter");

    lua_pushstring(L, "hooks");
    lua_createtable(L, 0, 1); {
      // TODO: LUA_KV_SET_STR(L, "post_checkout", "function() vim.cmd('TSUpdate') end");
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "nvim-treesitter");
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_TYPE(L, string, "auto_install", boolean, true);

    lua_pushstring(L, "highlight");
    lua_createtable(L, 0, 1); {
      LUA_KV_SET_TYPE(L, string, "enable", boolean, false);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "indent");
    lua_createtable(L, 0, 1); {
      LUA_KV_SET_TYPE(L, string, "enable", boolean, false);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);
  // TODO: auto build

  // Highlight Special Comments
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_STR(L, "source", "folke/todo-comments.nvim");

    lua_pushstring(L, "depends");
    lua_createtable(L, 1, 0); {
      lua_pushstring(L, "nvim-lua/plenary.nvim");
      lua_rawseti(L, -2, 1);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

do_cmdline_cmd("lua require('todo-comments').setup { \
  signs = false, \
  search = { \
    pattern = [[\\b(KEYWORDS)(\\([^\\)]*\\))?:]], \
  }, \
  highlight = { \
    before = '', \
    keyword = 'bg', \
    after = '', \
    pattern = [[.*<((KEYWORDS)%(\\(.{-1,}\\))?):]], \
  }, \
}");
  nvim_map(L, "n", "<leader>st", "<cmd>TodoTelescope<cr>");

  // Highlight Color Codes
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "catgoose/nvim-colorizer.lua");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "colorizer");
  lua_createtable(L, 0, 1); {
    lua_pushstring(L, "user_default_options");
    lua_createtable(L, 0, 1); {
      LUA_KV_SET_TYPE(L, string, "names", boolean, false);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);
  nvim_map(L, "n", "<leader>uh", "<cmd>Colortils<cr>");

  // Edit Color Codes
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 1); {
    LUA_KV_SET_STR(L, "source", "max397574/colortils.nvim");
  }
  LUA_PCALL_VOID(L, 1, 0);

  LUA_REQUIRE_SETUP(L, "colortils");
  LUA_PCALL_VOID(L, 0, 0);

  // Theme
  LUA_MINIDEPS_ADD(L);
  lua_createtable(L, 0, 2); {
    LUA_KV_SET_STR(L, "source", "zenbones-theme/zenbones.nvim");

    lua_pushstring(L, "depends");
    lua_createtable(L, 1, 0); {
      lua_pushstring(L, "rktjmp/lush.nvim");
      lua_rawseti(L, -2, 1);
    }
    lua_settable(L, -3);
  }
  LUA_PCALL_VOID(L, 1, 0);

  nvim_set_o(L, "background", nvim_mk_obj_string("light"));

  // TODO: nvim_set_g("tokyobones", = {
  //   lightness = 'bright',
  //   transparent_background = true,
  //   darken_comments = 45,
  // });
  // do_cmdline_cmd("colorscheme tokyobones");

  // highlights
  {
    Dict(highlight) hl = {};
    PUT_KEY(hl, highlight, bg, nvim_mk_obj_string("#C7CBDB"));
    nvim_highlight(L, "ColorColumn", hl);
  }
  {
    Dict(highlight) hl = {};
    PUT_KEY(hl, highlight, fg, nvim_mk_obj_string("#D0D1D8"));
    nvim_highlight(L, "Whitespace", hl);
  }


  /* POST OPTIONS */
  /* Keymaps */
  // Auto Completion
  nvim_map(L, "i", "<c-space>", "<c-x><c-u>");

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
  nvim_map(L, "n", "<leader>uq", "<cmd>copen<cr>");
  nvim_map(L, "n", "<leader>ul", "<cmd>lopen<cr>");

  // Code
  nvim_map(L, "n", "<leader>cw", "<cmd>cd %:p:h<cr>"); // move nvim base path to current buffer
  nvim_map(L, "n", "<leader>cm", "<cmd>Man<cr>");
  nvim_map(L, "n", "<leader>cW", "<cmd>s/\\s\\+$//g<cr>"); // remove trailing whitespace

  // Make
  /* TODO: Make
nvim_map(L, "n", "<leader>mm", "<cmd>make<cr>");
  local recent_make_command = 'make'
  vim.keymap.set('n', '<leader>mc', function()
    vim.ui.input({ prompt = 'Make Command: ', default = recent_make_command }, function(usr_input)
    recent_make_command = usr_input
    calm_error(vim.cmd, 'set makeprg=' .. escape_string(recent_make_command))
    end)
  end, { desc = 'Make [C]reate Command' })
  */

  // UI
  nvim_map(L, "n", "<leader>um", "<cmd>messages<cr>");

  // Toggle
  nvim_map(L, "n", "<leader>th", "<cmd>ColorizerToggle<cr>");

  // Panes
  nvim_map(L, "n", "<leader>v", "<cmd>vsp<cr>");
  nvim_map(L, "n", "<leader>pv", "<cmd>vsp<cr>");
  nvim_map(L, "n", "<leader>x", "<cmd>sp<cr>");
  nvim_map(L, "n", "<leader>px", "<cmd>sp<cr>");
  nvim_map(L, "n", "<leader>pt", "<cmd>tab split<cr>");

  nvim_map(L, "n", "<leader>p|", "<cmd>vertical resize<cr>");
  nvim_map(L, "n", "<leader>p_", "<cmd>horizontal resize<cr>");
  nvim_map(L, "n", "<leader>pZ", "<cmd>wincmd =<cr>");
  nvim_map(L, "n", "<leader>pz", "<cmd>horizontal resize<cr><cmd>vertical resize<cr>");

  nvim_map(L, "n", "<leader>pN", "<cmd>setlocal buftype=nofile<cr>"); // turn off ability to save
  /* End Keymaps */

  /* Auto Cmds */
  // Highlight when yanking (copying) text
  nvim_mk_augroup_command(L, "TextYankPost", "Highlight when yanking (copying) text", "my-highlight-yank", true,
      nvim_mk_string("lua vim.highlight.on_yank({ on_visual = false })"));

  /* RETURNS */
  lua_newtable(L);

  // register completion function
  lua_pushcfunction(L, completer);
  lua_setfield(L, -2, "completer"); // TODO: COMPLETER DOES NOT WORK

  /* EXIT */
  deinit_arena(&string_arena);
  return 1;
}
