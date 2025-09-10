#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>

/* macros */
#define PROGRAM "make_c"
#define PANIC_FMT(fmt, ...) { fprintf(stderr, PROGRAM ": ERROR: " fmt, __VA_ARGS__); exit(-1); }
#define PANIC(msg) fprintf(stderr, PROGRAM ": ERROR: " msg)
#define ASSERT(b, msg) do { if(!(b)) { PANIC(msg); } } while(0)

#define STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

/* limits */
#define ARGS_MAX 4096

/* types */
#define MAKEMODE_LIST \
  MAKEMODE_X(Debug) \
  MAKEMODE_X(Release)

enum MakeMode : int
{
#define MAKEMODE_X(n) MakeMode_##n,
  MAKEMODE_LIST
#undef MAKEMODE_X
  MakeMode__Count,
};

/* helpers */
static inline int
copy_args(
    char **prog_args,
    size_t prog_args_max,
    char **buffer,
    size_t buffer_len)
{
  if(prog_args_max < buffer_len) { return 0; }

  for(size_t i = 0;
      i < buffer_len;
      i++)
  {
    prog_args[i] = buffer[i];
  }

  return 1;
}

/* main */
int
main(
    int argc,
    char **argv,
    char **envp)
{
  /* vars */
  char *warn_flags[] = {
    "-Wall",
    "-Wextra",
    "-Wpedantic",
  };
  size_t warn_flags_len = STATIC_ARRAY_SIZE(warn_flags);

  char *shared_lib_flags[] = {
    "-shared",
    "-fPIC",
    "-Wl,-undefined,dynamic_lookup",
  };
  size_t shared_lib_flags_len = STATIC_ARRAY_SIZE(shared_lib_flags);

  /* targets */
  char *release_flags[] = {
    "-Werror",
    "-O3",
    "-march=native",
  };
  size_t release_flags_len = STATIC_ARRAY_SIZE(release_flags);

  char *debug_flags[] = {
    "-Og",
    "-g",
    "-DDEBUG",
  };
  size_t debug_flags_len = STATIC_ARRAY_SIZE(debug_flags);

  char *source_names[] = {
    "config.c"
  };
  size_t source_names_len = STATIC_ARRAY_SIZE(source_names);

  char *output_name = "config.so";

  /* arg parse */
  if(argc == 0) { return -1; }

  argc--; // Ignore program name, only works on kernels that give us the prog name first
  argv++;

  /* get args */
  char *prog_args[ARGS_MAX] = {"/usr/bin/gcc"}; // WARN: max number of args to gcc, i dont think kernel can support more than that lol
  size_t prog_args_len = 1;
  char *extra_args[ARGS_MAX] = {0};
  size_t extra_args_len = 0;
  while(argc > 0)
  {
    if(argv[0][0] != '-')
    {
      break; // there are no more flag args (please dont put them after, im begging you)
    }
    else if(strncmp(argv[0], "--makeprg=", sizeof("--makeprg=") - 1) == 0)
    {
      prog_args[0] = argv[0] + sizeof("--makeprg=") - 1;
    }
    else
    {
      extra_args[extra_args_len++] = argv[0];
    }
    argv++;
    argc--;
  }

  /* get current mode */
  enum MakeMode make_mode = MakeMode_Debug;
  if(argc == 0 || strcmp(argv[0], "debug") == 0)
  {
  }
  else if(strcmp(argv[0], "release") == 0)
  {
    make_mode = MakeMode_Release;
  }
  else
  {
    PANIC_FMT("unknown make mode: %s\n", argv[0]);
  }

  /* setup build */
  // warn
  ASSERT(copy_args(
        prog_args + prog_args_len, ARGS_MAX - prog_args_len,
        warn_flags, warn_flags_len),
      "ran out of args\n");
  prog_args_len += warn_flags_len;

  // extra_args
  ASSERT(copy_args(
        prog_args + prog_args_len, ARGS_MAX - prog_args_len,
        extra_args, extra_args_len),
      "ran out of args\n");
  prog_args_len += extra_args_len;

  switch(make_mode)
  {
  default: {
    PANIC_FMT("expected make mode to be less than %d, got %d\n", MakeMode__Count, make_mode);
  } break;

  case MakeMode_Debug: {
    ASSERT(copy_args(
          prog_args + prog_args_len, ARGS_MAX - prog_args_len,
          debug_flags, debug_flags_len),
        "ran out of args\n");
    prog_args_len += debug_flags_len;
  } break;

  case MakeMode_Release: {
    ASSERT(copy_args(
          prog_args + prog_args_len, ARGS_MAX - prog_args_len,
          release_flags, release_flags_len),
        "ran out of args\n");
    prog_args_len += release_flags_len;
  } break;
  }

  // source
  ASSERT(copy_args(
        prog_args + prog_args_len, ARGS_MAX - prog_args_len,
        source_names, source_names_len),
      "ran out of args\n");
  prog_args_len += source_names_len;

  // shared lib
  ASSERT(copy_args(
        prog_args + prog_args_len, ARGS_MAX - prog_args_len,
        shared_lib_flags, shared_lib_flags_len),
      "ran out of args\n");
  prog_args_len += shared_lib_flags_len;

  ASSERT(prog_args_len + 1 < ARGS_MAX, "ran out of args\n");
  prog_args[prog_args_len++] = "-o";

  ASSERT(prog_args_len + 1 < ARGS_MAX, "ran out of args\n");
  prog_args[prog_args_len++] = output_name;

  ASSERT(prog_args_len + 1 < ARGS_MAX, "ran out of args\n");
  prog_args[prog_args_len++] = NULL;

  /* call the build */
  for(size_t i = 0;
      i < prog_args_len - 1; // ignore the null
      i++)
  {
    printf("%s ", prog_args[i]);
  }
  putchar('\n');
  execve(prog_args[0], prog_args, envp);

  /* exec failed */
  PANIC_FMT("execve failed: error_code(%d): program('%s')\n", errno, prog_args[0]);
  return -1;
}
