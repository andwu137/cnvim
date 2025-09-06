// TODO: reduce the size of lua
// TODO: replace any `do_cmdline_cmd` if possible
// TODO: replace any `lua MiniDeps.add` if possible
// TODO: replace any `lua require` if possible
// TODO: test if the plugins/binds actually work

#include <stdio.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>

#include "nvim_api.c"

#include "config.h"
#include "arena.c"
#include "fileio.c"

/* GLOBALS */
char const g_package_dir[] = "/site/";
char const g_mini_plugin_dir[] = "pack/deps/opt/mini.nvim";
char const g_install_mini_nvim_command[] =
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
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR: %s\n", e.msg); }
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
  if(e.type != kErrorTypeNone) { PANIC_FMT(L, "ERROR: %s\n", e.msg); }
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
  if(e.type != kErrorTypeNone) { PANIC_FMT((L), "ERROR: %s\n", e.msg); }
}

static inline void
nvim_highlight(
    lua_State *L,
    char *group,
    Dict(highlight) opts)
{
  Error e = ERROR_INIT;
  nvim_set_hl(0, 0, nvim_mk_string(group), &opts, &e);
  if(e.type != kErrorTypeNone) { PANIC_FMT((L), "ERROR: %s\n", e.msg); }
}

/* MAIN */
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
  //   nvim_set_o(L, "cursorline", nvim_mk_obj_bool(true));

  // Minimal number of screen lines to keep above and below the cursor.
  nvim_set_o(L, "scrolloff", nvim_mk_obj_int(1));
  nvim_set_o(L, "sidescrolloff", nvim_mk_obj_int(20));

  // Tabs
  nvim_set_o(L, "tabstop", nvim_mk_obj_int(4));
  nvim_set_o(L, "softtabstop", nvim_mk_obj_int(4));
  nvim_set_o(L, "shiftwidth", nvim_mk_obj_int(4));
  //   nvim_set_o(L, "expandtab", nvim_mk_obj_bool(true));

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

  /*
  Object complete_menu = nvim_mk_obj_string("longest,noinsert,noselect");
  nvim_set_g(L, "complete_menu", complete_menu);
  nvim_set_o(L, "completeopt", complete_menu);
  nvim_set_o(L, "pumheight", nvim_mk_obj_int(4));
  */


  /* SETUP THE PACKAGE MANAGER */
  // require the package manager
  do_cmdline_cmd(STRINGIFY(packadd mini.nvim | helptags ALL));
  {
    char *prelude = "lua require('mini.deps').setup { path = { package = '";
    char *postlude = "' } }";
    uint require_pos = string_arena.length;
    ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)prelude, strlen(prelude)));
    ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)plugin_path, plugin_path_len));
    ASSERT(L, copy_alloc_arena(&string_arena, (uint8_t *)postlude, strlen(postlude)));
    do_cmdline_cmd(string_arena.buffer + require_pos);
    string_arena.length = require_pos;
    string_arena.buffer[string_arena.length] = 0;
  }

  // download stuff
  {
do_cmdline_cmd("lua require('mini.ai').setup { n_lines = 500 }");
do_cmdline_cmd("lua require('mini.bracketed').setup {}");
do_cmdline_cmd("lua require('mini.comment').setup {\
  options = {\
    custom_commentstring = function()\
      local remap = {\
        verilog = '/* %s */',\
      }\
      for ft, cm in pairs(remap) do\
        if vim.bo.ft == ft then\
          return cm\
        end\
      end\
      return nil\
    end,\
    ignore_blank_line = true,\
  },\
}");

// Detect Tabbing
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'tpope/vim-sleuth',\
}");

// Git Signs
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'lewis6991/gitsigns.nvim',\
}");
do_cmdline_cmd("lua require('gitsigns').setup {\
  signs = {\
    add = { text = '+' },\
    change = { text = '~' },\
    delete = { text = '_' },\
    topdelete = { text = '‾' },\
    changedelete = { text = '~' },\
  },\
}");
// TODO: setup hunk binds

// File Explorer
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'stevearc/oil.nvim',\
}");
do_cmdline_cmd("lua require('oil').setup {\
  columns = {\
    --[[ 'permissions',]]\
    --[[ 'size',]]\
    'mtime',\
    'icon',\
  },\
  natural_order = true,\
  delete_to_trash = true,\
  keymaps = {\
    ['g?'] = 'actions.show_help',\
    ['<CR>'] = 'actions.select',\
    ['L'] = 'actions.select',\
    ['H'] = 'actions.parent',\
    ['<C-c>'] = 'actions.close',\
    ['<C-l>'] = 'actions.refresh',\
    ['g.'] = 'actions.toggle_hidden',\
    ['g\\\\'] = 'actions.toggle_trash',\
  },\
  view_options = {\
    show_hidden = true,\
  },\
}");
// TODO: setup binding

// Visual Undo Tree
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'jiaoshijie/undotree',\
  depends = { 'nvim-lua/plenary.nvim' },\
}");
do_cmdline_cmd("lua require('undotree').setup {}");
// TODO: setup binding

// Show Keybinds
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'folke/which-key.nvim',\
}");
do_cmdline_cmd("lua require('which-key').setup { delay = 300 }");

do_cmdline_cmd("lua MiniDeps.add {\
  source = 'nvim-telescope/telescope.nvim',\
  depends = {\
    { source = 'nvim-lua/plenary.nvim' },\
    {\
      source = 'nvim-telescope/telescope-fzf-native.nvim',\
      build = 'make',\
      cond = function()\
        return vim.fn.executable 'make' == 1\
      end,\
    },\
  },\
}");
do_cmdline_cmd("lua require('telescope').setup {}");
do_cmdline_cmd("lua pcall(require('telescope').load_extension, 'fzf')");
do_cmdline_cmd("lua pcall(require('telescope').load_extension, 'ui-select')");
// TODO: setup bindings

// QOL Improvements for marks
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'chentoast/marks.nvim',\
}");
do_cmdline_cmd("lua require('marks').setup {\
  default_mappings = true,\
  mappings = {},\
}");

// Jump To Files
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'ThePrimeagen/harpoon',\
  checkout = 'harpoon2',\
  depends = { 'nvim-lua/plenary.nvim' }\
}");
do_cmdline_cmd("lua require('harpoon'):setup()");
// TODO: setup bindings

// SUGGEST: do we want to support LSP?

// Auto Format
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'stevearc/conform.nvim',\
}");
do_cmdline_cmd("lua require('conform').setup {\
  formatters_by_ft = {\
    c = { 'clang-format' },\
    cpp = { 'clang-format' },\
    clojure = { 'cljfmt' },\
    haskell = { 'fourmolu', 'ormolu', stop_after_first = true },\
    html = { 'prettier' },\
    java = { 'google-java-format' },\
    lua = { 'stylua' },\
    odin = { 'odinfmt' },\
    purescript = { 'purescript-tidy' },\
    python = function(bufnr)\
      if require('conform').get_formatter_info('ruff_format', bufnr).available then\
        return { 'ruff_format' }\
      else\
        return { 'isort', 'black' }\
      end\
    end,\
    rust = { 'rustfmt' },\
    cs = { 'csharpier' },\
    typescript = { 'biome' },\
    javascript = { 'biome' },\
  },\
  formatters = {\
    odinfmt = {\
      --[[ Change where to find the command if it isn't in your path.]]\
      command = 'odinfmt',\
      args = { '-stdin' },\
      stdin = true,\
    },\
  },\
}");
// TODO: setup bindings

// Text Semantics Engine
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'nvim-treesitter/nvim-treesitter',\
}");
do_cmdline_cmd("lua require('nvim-treesitter').setup {\
  auto_install = true,\
  highlight = { enable = false },\
  indent = { enable = false },\
}");
// TODO: auto build

// Highlight Special Comments
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'folke/todo-comments.nvim',\
  depends = { 'nvim-lua/plenary.nvim' },\
}");
do_cmdline_cmd("lua require('todo-comments').setup {\
  signs = false,\
  search = {\
    pattern = [[\\b(KEYWORDS)(\\([^\\)]*\\))?:]],\
  },\
  highlight = {\
    before = '',\
    keyword = 'bg',\
    after = '',\
    pattern = [[.*<((KEYWORDS)%(\\(.{-1,}\\))?):]],\
  },\
}");

// Highlight Color Codes
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'catgoose/nvim-colorizer.lua',\
}");
do_cmdline_cmd("lua require('colorizer').setup {\
  user_default_options = { names = false },\
}");

// Edit Color Codes
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'max397574/colortils.nvim',\
}");
do_cmdline_cmd("lua require('colortils').setup {}");

// Theme
do_cmdline_cmd("lua MiniDeps.add {\
  source = 'zenbones-theme/zenbones.nvim',\
}");

nvim_set_o(L, "background", nvim_mk_obj_string("light"));

// TODO: nvim_set_g("tokyobones", = {
//   lightness = 'bright',
//   transparent_background = true,
//   darken_comments = 45,
// });

// TODO: do_cmdline_cmd("colorscheme tokyobones");

// TODO: nvim_highlight(L, "ColorColumn", { .bg = nvim_mk_obj_string("#C7CBDB") });
// TODO: nvim_highlight(L, "Whitespace", { .fg = nvim_mk_obj_string("#D0D1D8") });
  }


  /* POST OPTIONS */
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

  // Git Signs Hunks
  nvim_map(L, "n", "]h", "<cmd>lua require('gitsigns').nav_hunk('next')<cr>");
  nvim_map(L, "n", "[h", "<cmd>lua require('gitsigns').nav_hunk('prev')<cr>");

  // Search
  nvim_map(L, "n", "<leader>st", "<cmd>TodoTelescope<cr>");
  nvim_map(L, "n", "<leader>sn", "<cmd>lua vim.cmd('e ' .. vim.fn.stdpath 'config')<cr>");
  nvim_map(L, "n", "<leader>sf", "<cmd>lua require('telescope.builtin').find_files()<cr>");
  nvim_map(L, "n", "<leader>sg", "<cmd>lua require('telescope.builtin').live_grep()<cr>");
  nvim_map(L, "n", "<leader>sd", "<cmd>lua require('telescope.builtin').diagnostics()<cr>");
  nvim_map(L, "n", "<leader>s.", "<cmd>lua require('telescope.builtin').oldfiles()<cr>");
  nvim_map(L, "n", "<leader>so", "<cmd>lua require('telescope.builtin').buffers()<cr>");
  nvim_map(L, "n", "<leader>sm", "<cmd>lua require('telescope.builtin').marks()<cr>");

  // Oil
  nvim_map(L, "n", "<leader>uf", "<cmd>Oil<cr>");

  // Auto Format
  nvim_map(L, "n", "<leader>cf", "require('conform').format({ async = true, lsp_format = 'fallback' })");

  // Colortils
  nvim_map(L, "n", "<leader>uh", "<cmd>Colortils<cr>");

  // Undo Tree
  nvim_map(L, "n", "<leader>cu", "<cmd>lua require('undotree').toggle()<cr>");
  /* End Keymaps */

  /* Auto Cmds */
  // Highlight when yanking (copying) text
  /* TODO: autocmd
  vim.api.nvim_create_autocmd('TextYankPost', {
    desc = 'Highlight when yanking (copying) text',
    group = vim.api.nvim_create_augroup('my-highlight-yank', { clear = true }),
    callback = function()
    vim.highlight.on_yank { on_visual = false }
    end,
  })
  */

  /* EXIT */
  deinit_arena(&string_arena);
  return 0;
}
