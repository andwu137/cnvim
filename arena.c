#ifndef ARENA_C
#define ARENA_C

struct Arena
{
  uint length;
  uint capacity;
  char *buffer;
};

static inline uint
amount_free_arena(
    struct Arena *a)
{
  return a->capacity - a->length;
}

static inline uint
init_arena(
    struct Arena *a,
    uint capacity)
{
  a->capacity = capacity;
  a->length = 0;
  a->buffer = calloc(a->capacity, sizeof(char)); // arbitary size for our command strings
  return a->buffer != NULL;
}

static inline void
deinit_arena(
    struct Arena *a)
{
  free(a->buffer);
  a->buffer = NULL; // "waste" one cpu cycle
}

static inline uint
alloc_arena(
    struct Arena *a,
    uint size)
{
  if(size <= 0 || size > amount_free_arena(a)) { return 0; }

  a->length += size;
  return 1;
}

static inline uint
free_arena(
    struct Arena *a,
    uint size)
{
  if(a->length < size)
  {
    a->length = 0;
    return 1;
  }

  a->length -= size;
  return 1;
}

static inline uint
clear_arena(
    struct Arena *a)
{
  return free_arena(a, a->length);
}

static inline uint
copy_alloc_arena(
    struct Arena *restrict a,
    uint8_t const *restrict buffer,
    uint buffer_len)
{
  if(buffer_len > amount_free_arena(a)) { return 0; }

  uint idx = a->length;
  if(alloc_arena(a, buffer_len) == 0) { return 0; }
  memcpy(a->buffer + idx, buffer, buffer_len);
  return 1;
}

static inline uint
replace_range_arena(
    struct Arena *restrict a,
    uint bottom, uint top,
    uint8_t const *restrict buffer,
    uint buffer_len)
{
  if(bottom >= top || top > a->length) { return 0; }

  uint above_top = a->length - top;
  if(top - bottom != buffer_len)
  {
    memmove(a->buffer + buffer_len, a->buffer + top, above_top);
  }

  memcpy(a->buffer + bottom, buffer, buffer_len);
  a->length = bottom + buffer_len + above_top;

  return 1;
}

#endif // ARENA_C
