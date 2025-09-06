package.cpath = package.cpath .. ";" .. vim.fn.stdpath('config') .. "/?.so"
require('config')
