This project is purely for fun (it was not fun), it will probably break on other computers / versions of nvim.

I tested this on `nvim-v0.11.3`, `luajit-5.1`.

My compilation steps are (and this could be specific to my machine):
```bash
git clone https://github.com/andwu137/cnvim.git
make release LUA_CFLAGS="-I/usr/include/luajit-2.1/ -lluajit-5.1"
```

Sources:
- https://github.com/neovim/neovim/blob/master/src/nvim/api/private/defs.h
- https://github.com/rewhile/CatNvim
- Build neovim, then grep the files (include the hidden ones in build) for the generated header files
    - Yes, there is a way to link the generated headers directly, but I copy pasted cause I'm weak
