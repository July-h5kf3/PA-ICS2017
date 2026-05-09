#include "fs.h"

typedef struct {
  char *name;
  size_t size;
  off_t disk_offset;
  off_t open_offset;
} Finfo;

enum {FD_STDIN, FD_STDOUT, FD_STDERR, FD_FB, FD_EVENTS, FD_DISPINFO, FD_NORMAL};

/* This is the information about all files in disk. */
static Finfo file_table[] __attribute__((used)) = {
  {"stdin (note that this is not the actual stdin)", 0, 0},
  {"stdout (note that this is not the actual stdout)", 0, 0},
  {"stderr (note that this is not the actual stderr)", 0, 0},
  [FD_FB] = {"/dev/fb", 0, 0},
  [FD_EVENTS] = {"/dev/events", 0, 0},
  [FD_DISPINFO] = {"/proc/dispinfo", 128, 0},
#include "files.h"
};

#define NR_FILES (sizeof(file_table) / sizeof(file_table[0]))

void ramdisk_read(void *buf, off_t offset, size_t len);
void ramdisk_write(const void *buf, off_t offset, size_t len);
size_t events_read(void *buf, size_t len);
void dispinfo_read(void *buf, off_t offset, size_t len);
void fb_write(const void *buf, off_t offset, size_t len);

int fs_open(const char *pathname, int flags, int mode)
{
  (void)flags;
  (void)mode;

  for(int i = 0; i < NR_FILES; i++)
  {
    if(strcmp(pathname, file_table[i].name) == 0)
    {
      file_table[i].open_offset = 0;
      return i;
    }
  }
  assert(0);
  return -1;
}

size_t fs_filesz(int fd)
{
  assert(fd >= 0 && fd < NR_FILES);
  return file_table[fd].size;
}

size_t fs_read(int fd, void *buf, size_t len)
{
  assert(fd >= 0 && fd < NR_FILES);

  if (fd == FD_STDIN || fd == FD_STDOUT || fd == FD_STDERR) {
    return 0;
  }

  if (fd == FD_EVENTS) {
    return events_read(buf, len);
  }

  if (fd == FD_DISPINFO) {
    size_t remain = file_table[fd].size - file_table[fd].open_offset;
    if (len > remain) {
      len = remain;
    }
    dispinfo_read(buf, file_table[fd].open_offset, len);
    file_table[fd].open_offset += len;
    return len;
  }

  size_t remain = file_table[fd].size - file_table[fd].open_offset;
  if (len > remain) {
    len = remain;
  }

  ramdisk_read(buf, file_table[fd].disk_offset + file_table[fd].open_offset, len);
  file_table[fd].open_offset += len;
  return len;
}

size_t fs_write(int fd, const void *buf, size_t len)
{
  assert(fd >= 0 && fd < NR_FILES);

  if (fd == FD_STDOUT || fd == FD_STDERR) {
    for (size_t i = 0; i < len; i++) {
      _putc(((char *)buf)[i]);
    }
    return len;
  }

  if (fd == FD_STDIN || fd == FD_EVENTS || fd == FD_DISPINFO) {
    return 0;
  }

  if (fd == FD_FB) {
    size_t remain = file_table[fd].size - file_table[fd].open_offset;
    if (len > remain) {
      len = remain;
    }
    fb_write(buf, file_table[fd].open_offset, len);
    file_table[fd].open_offset += len;
    return len;
  }

  size_t remain = file_table[fd].size - file_table[fd].open_offset;
  if (len > remain) {
    len = remain;
  }

  ramdisk_write(buf, file_table[fd].disk_offset + file_table[fd].open_offset, len);
  file_table[fd].open_offset += len;
  return len;
}

off_t fs_lseek(int fd, off_t offset, int whence)
{
  assert(fd >= 0 && fd < NR_FILES);

  off_t new_offset = 0;
  switch (whence) {
    case SEEK_SET:
      new_offset = offset;
      break;
    case SEEK_CUR:
      new_offset = file_table[fd].open_offset + offset;
      break;
    case SEEK_END:
      new_offset = file_table[fd].size + offset;
      break;
    default:
      assert(0);
  }

  assert(new_offset >= 0 && new_offset <= file_table[fd].size);
  file_table[fd].open_offset = new_offset;
  return new_offset;
}

int fs_close(int fd)
{
  assert(fd >= 0 && fd < NR_FILES);
  return 0;
}

void init_fs() {
  // TODO: initialize the size of /dev/fb
  file_table[FD_FB].size = _screen.width * _screen.height * sizeof(uint32_t);
}
