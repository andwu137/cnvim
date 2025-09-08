FLAGS = -Wall -Wextra -Wpedantic
ALL_FLAGS = $(FLAGS) $(LUA_CFLAGS)

SHARED_LIB_FLAGS = -shared -fPIC -Wl,-undefined,dynamic_lookup

all: debug

release:
	gcc $(ALL_FLAGS) -Werror -O3 -march=native config.c $(SHARED_LIB_FLAGS) -o config.so

debug:
	gcc $(ALL_FLAGS) -Og -g config.c $(SHARED_LIB_FLAGS) -o config.so

.PHONY: all release debug
