#include <am.h>
#include <x86.h>

#define RTC_PORT 0x48   // Note that this is not standard
#define I8042_DATA_PORT 0x60
#define I8042_STATUS_PORT 0x64
#define I8042_STATUS_HASKEY_MASK 0x1

static unsigned long boot_time;

void _ioe_init() {
  boot_time = inl(RTC_PORT);
}

unsigned long _uptime() {
  return inl(RTC_PORT) - boot_time;
}

uint32_t* const fb = (uint32_t *)0x40000;

_Screen _screen = {
  .width  = 400,
  .height = 300,
};

extern void* memcpy(void *, const void *, int);

static inline int min(int a, int b) {
  return a < b ? a : b;
}

void _draw_rect(const uint32_t *pixels, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }

  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = min(x + w, _screen.width);
  int y1 = min(y + h, _screen.height);
  if (x0 >= x1 || y0 >= y1) {
    return;
  }

  int dst_w = x1 - x0;
  int src_x0 = x0 - x;
  int src_y0 = y0 - y;

  for (int j = 0; j < y1 - y0; j++) {
    const uint32_t *src = pixels + (src_y0 + j) * w + src_x0;
    memcpy(&fb[(y0 + j) * _screen.width + x0], src, dst_w * sizeof(uint32_t));
  }
}

void _draw_sync() {
  // x86-nemu's screen refresh is driven by NEMU's periodic device update.
  asm volatile("" : : : "memory");
}

int _read_key() {
  if (inb(I8042_STATUS_PORT) & I8042_STATUS_HASKEY_MASK) {
    return inl(I8042_DATA_PORT);
  }
  return _KEY_NONE;
}
