#ifndef FILEIO_C
#define FILEIO_C

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static inline long
get_file_size(
    int fd)
{
  struct stat stat_buf;
  if(fstat(fd, &stat_buf) == -1) { return -1; }

  return stat_buf.st_size;
}

static inline long
read_entire_file(
    char const *filename,
    char **out_buf)
{
  int fd = open(filename, O_RDONLY);
  long file_size;
  if((file_size = get_file_size(fd)) == -1) { return -1; }

  *out_buf = malloc(file_size);
  if(*out_buf == NULL) { return -1; }

  if(read(fd, *out_buf, file_size) != file_size) { free(*out_buf); return -1; }

  return file_size;
}
#endif

#endif // FILEIO_C
