# C Nvim - Neovim config in C
This project is purely for fun (it was not fun), it will probably break on other computers / versions of nvim.
I would guess this has a worse startup time than jit-ed lua (unverified).

I tested this on `nvim-v0.11.5`, `LuaJIT-2.1.1761727121`, `gcc-15.2.1`, `linux-x86_64`, `glibc-2.41`.

Build Requires:
- `gcc` or `any other C compiler that supports this relatively standard C`
- `luajit-devel` (Lua C API development headers)
- `pkg-config` (get library flags for C compiler)

Requires:
- `neovim` (duh)

My compilation steps are (and this could be specific to my machine):
```bash
git clone https://github.com/andwu137/cnvim.git

# remove -march=native from the source code, if you plan on distributing this to another computer
gcc -Wall -Wextra -Wpedantic -O3 -march=native make.c -o make_c
./make_c release -DMODE_FORMATTER -DMODE_DESIGN -DMODE_THEME # -DMODE_FOCUS -DPERFORMANCE -DDEBUG

# or on my computer, use ./make.sh
```

Sources:
- The Lua C API Reference (get the right version): https://www.lua.org/manual/5.1/
- Build neovim, then grep the files (include the hidden ones in build) for the generated header files
    - Yes, there is a way to link the generated headers directly, but I copy pasted cause I'm weak
- https://github.com/neovim/neovim/blob/master/src/nvim/api/private/defs.h
- https://github.com/rewhile/CatNvim
