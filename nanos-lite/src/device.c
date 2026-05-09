#include "common.h"

#define NAME(key) \
  [_KEY_##key] = #key,

static const char *keyname[256] __attribute__((used)) = {
  [_KEY_NONE] = "NONE",
  _KEYS(NAME)
};

size_t events_read(void *buf, size_t len) {
  static unsigned long last_time = 0;
  int key = _read_key();

  if (key != _KEY_NONE) {
    bool keydown = (key & 0x8000) != 0;
    int code = key & ~0x8000;
    return snprintf(buf, len, "%s %s\n", keydown ? "kd" : "ku", keyname[code]);
  }

  unsigned long now = _uptime();
  if (now != last_time) {
    last_time = now;
    return snprintf(buf, len, "t %lu\n", now);
  }

  return 0;
}

static char dispinfo[128] __attribute__((used));

void dispinfo_read(void *buf, off_t offset, size_t len) {
  memcpy(buf, dispinfo + offset, len);
}

void fb_write(const void *buf, off_t offset, size_t len) {
  assert(offset % sizeof(uint32_t) == 0);
  assert(len % sizeof(uint32_t) == 0);

  const uint32_t *pixels = (const uint32_t *)buf;
  size_t pixel_offset = offset / sizeof(uint32_t);
  size_t pixel_len = len / sizeof(uint32_t);

  while (pixel_len > 0) {
    int x = pixel_offset % _screen.width;
    int y = pixel_offset / _screen.width;
    int w = _screen.width - x;
    if ((size_t)w > pixel_len) {
      w = pixel_len;
    }

    _draw_rect(pixels, x, y, w, 1);
    pixels += w;
    pixel_offset += w;
    pixel_len -= w;
  }

  _draw_sync();
}

void init_device() {
  _ioe_init();

  int len = snprintf(dispinfo, sizeof(dispinfo), "WIDTH:%d\nHEIGHT:%d\n",
      _screen.width, _screen.height);
  assert(len >= 0 && len < sizeof(dispinfo));
}
