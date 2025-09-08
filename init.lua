package.cpath = package.cpath .. ";" .. vim.fn.stdpath('config') .. "/?.so"
config_c = require('config')
vim.o.completefunc = "v:lua.config_c.completer"
