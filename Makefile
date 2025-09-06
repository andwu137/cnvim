FLAGS = -Wall -Wextra -Wpedantic -Wextra

all: debug

release:
	gcc $(FLAGS) -O3 -march=native config.c -shared -fPIC -Wl,-undefined,dynamic_lookup -o config.so

debug:
	gcc $(FLAGS) -Og -g config.c -shared -fPIC -Wl,-undefined,dynamic_lookup -o config.so
