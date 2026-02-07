package.cpath = package.cpath .. ";" .. vim.fn.stdpath('config') .. "/?.so"
require('config')

if vim.g.neovide then
  vim.keymap.set('v', '<sc-c>', '"+y', {noremap = true})
  vim.keymap.set({'n', 'v'}, '<sc-v>', '"+P', {noremap = true})
  vim.keymap.set('i', '<sc-v>', '<esc>l"+p', {noremap = true})

  vim.g.neovide_position_animation_length = 0
  vim.g.neovide_cursor_animation_length = 0.01
  vim.g.neovide_cursor_trail_size = 0
  vim.g.neovide_cursor_animate_in_insert_mode = false
  vim.g.neovide_cursor_animate_command_line = false
  vim.g.neovide_scroll_animation_far_lines = 0
  vim.g.neovide_scroll_animation_length = 0.01

  vim.o.guifont = 'FiraCode Nerd Font:h18'
end
