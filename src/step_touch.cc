// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// MANTRA -- touch-to-value for the step faders. See step_touch.h. The DRM read-back and the multitouch reader follow
// CSXXL's XYZ pad (src/xyz_pad.c), cut down: no overlay plane, no drawing, read-only on the display.
#define _GNU_SOURCE
#include "step_touch.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <linux/input.h>
#include "param_ids.h"

namespace stp {
namespace {

const int PAGE_W = 1280, PAGE_H = 628;
const int INSET = 17;          // the thumb centre's travel starts this far in from the slot's ends (measured on the 35 frame strip: 16.5 at the top, 17.5 at the bottom)
const int MARGIN_Y = 16;       // a touch this far above / below the faders still counts

char g_log[700];
int g_nlog = 0;
void logf_(const char *fmt, ...) {
  if (!g_log[0] || g_nlog > 600) return;
  FILE *f = fopen(g_log, "a");
  if (!f) return;
  g_nlog++;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  fprintf(f, "[%6ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

int xioctl(int fd, unsigned long req, void *arg) {
  int r;
  do r = ioctl(fd, req, arg); while (r < 0 && (errno == EINTR || errno == EAGAIN));
  return r;
}

// ---------------------------------------------------------------------------------------------- the display
struct Disp {
  int fd = -1;
  uint32_t crtc_id = 0;
  int fb_err_logged = 0, fb_logged = 0;
  bool have_page = false, on_screen = false, fmt_unknown = false;
  int rot = 0, lw = 0, lh = 0, box_x = 0, box_y = 0;
};

struct FbMap { uint8_t *map = nullptr, *base = nullptr; size_t len = 0; uint32_t pitch = 0, w = 0, h = 0, fmt = 0; };

int scanout_open(Disp &d, FbMap &m) {
  m = FbMap();
  struct drm_mode_crtc c;
  memset(&c, 0, sizeof c);
  c.crtc_id = d.crtc_id;
  if (xioctl(d.fd, DRM_IOCTL_MODE_GETCRTC, &c) || !c.fb_id) return -1;
  struct drm_mode_fb_cmd2 f;
  memset(&f, 0, sizeof f);
  f.fb_id = c.fb_id;
  if (xioctl(d.fd, DRM_IOCTL_MODE_GETFB2, &f)) {
    struct drm_mode_fb_cmd f1;
    memset(&f1, 0, sizeof f1);
    f1.fb_id = c.fb_id;
    if (xioctl(d.fd, DRM_IOCTL_MODE_GETFB, &f1)) { if (!d.fb_err_logged++) logf_("GETFB2 and GETFB %u: %s", c.fb_id, strerror(errno)); return -1; }
    memset(&f, 0, sizeof f);
    f.fb_id = f1.fb_id;
    f.width = f1.width;
    f.height = f1.height;
    f.pitches[0] = f1.pitch;
    f.handles[0] = f1.handle;
    f.pixel_format = f1.bpp == 32 ? DRM_FORMAT_XRGB8888 : f1.bpp == 24 ? DRM_FORMAT_RGB888 : f1.bpp == 16 ? DRM_FORMAT_RGB565 : 0;
  }
  if (!f.handles[0]) { if (!d.fb_err_logged++) logf_("GETFB %u: no handle", c.fb_id); return -1; }
  struct drm_prime_handle p;
  memset(&p, 0, sizeof p);
  p.handle = f.handles[0];
  p.flags = DRM_CLOEXEC;
  int rc = xioctl(d.fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p);
  struct drm_gem_close gc;
  memset(&gc, 0, sizeof gc);
  gc.handle = f.handles[0];
  xioctl(d.fd, DRM_IOCTL_GEM_CLOSE, &gc);   // GETFB gave a handle of our own: drop it at once
  if (rc) { if (!d.fb_err_logged++) logf_("PRIME export of fb %u: %s", c.fb_id, strerror(errno)); return -1; }
  size_t len = (size_t)f.pitches[0] * f.height + f.offsets[0];
  uint8_t *mm = (uint8_t *)mmap(NULL, len, PROT_READ, MAP_SHARED, p.fd, 0);
  close(p.fd);
  if (mm == MAP_FAILED) { if (!d.fb_err_logged++) logf_("mmap of fb %u: %s", c.fb_id, strerror(errno)); return -1; }
  m.base = mm;
  m.map = mm + f.offsets[0];
  m.len = len;
  m.pitch = f.pitches[0];
  m.w = f.width;
  m.h = f.height;
  m.fmt = f.pixel_format;
  if (!d.fb_logged) { d.fb_logged = 1; logf_("MPC's picture: fb %u, %ux%u %.4s, pitch %u", c.fb_id, f.width, f.height, (char *)&f.pixel_format, f.pitches[0]); }
  return 0;
}
// mapped for one look and released at once: a mapping kept would pin MPC's buffer
void scanout_close(FbMap &m) {
  if (m.base) munmap(m.base, m.len);
  m = FbMap();
}

// the display: a DRM card fd among this process's files that has an active CRTC with a picture we can read
bool drm_find(Disp &d) {
  DIR *dir = opendir("/proc/self/fd");
  if (!dir) { logf_("no /proc/self/fd: %s", strerror(errno)); return false; }
  int fds[8], n = 0;
  struct dirent *e;
  while ((e = readdir(dir)) && n < 8) {
    char link[64], target[256];
    int fd = atoi(e->d_name);
    if (fd < 3) continue;
    snprintf(link, sizeof link, "/proc/self/fd/%s", e->d_name);
    ssize_t ln = readlink(link, target, sizeof target - 1);
    if (ln <= 0) continue;
    target[ln] = 0;
    if (strncmp(target, "/dev/dri/card", 13)) continue;
    fds[n++] = fd;
  }
  closedir(dir);
  if (!n) { logf_("no DRM card among this process's files"); return false; }
  for (int i = 0; i < n; i++) {
    struct drm_mode_card_res r;
    memset(&r, 0, sizeof r);
    if (xioctl(fds[i], DRM_IOCTL_MODE_GETRESOURCES, &r)) continue;
    uint32_t crtcs[16], nc = r.count_crtcs > 16 ? 16 : r.count_crtcs;
    memset(&r, 0, sizeof r);
    r.crtc_id_ptr = (uintptr_t)crtcs;
    r.count_crtcs = nc;
    if (xioctl(fds[i], DRM_IOCTL_MODE_GETRESOURCES, &r)) continue;
    for (uint32_t k = 0; k < nc; k++) {
      struct drm_mode_crtc c;
      memset(&c, 0, sizeof c);
      c.crtc_id = crtcs[k];
      if (xioctl(fds[i], DRM_IOCTL_MODE_GETCRTC, &c) || !c.fb_id) continue;
      Disp t;
      t.fd = fds[i];
      t.crtc_id = c.crtc_id;
      FbMap m;
      if (scanout_open(t, m) == 0) {
        scanout_close(m);
        d.fd = t.fd;
        d.crtc_id = t.crtc_id;
        logf_("display: fd %d crtc %u (%ux%u)", d.fd, d.crtc_id, c.mode.hdisplay, c.mode.vdisplay);
        return true;
      }
    }
  }
  logf_("no DRM fd gives a readable picture");
  return false;
}

// ---------------------------------------------------------------------------------------------- the page's marker pixels
// 2 x 2 pixels at the page's top-left and bottom-right (tools/art/ui_art.py metal()), in these colours (R, G, B in reading order)
const uint8_t MARK_TL[4][3] = {{19, 11, 83}, {83, 11, 19}, {11, 83, 19}, {19, 83, 11}};
const uint8_t MARK_BR[4][3] = {{83, 19, 11}, {11, 19, 83}, {57, 11, 57}, {11, 57, 57}};

inline int px_rgb(const FbMap &m, int x, int y, uint8_t *rgb) {
  if (x < 0 || y < 0 || x >= (int)m.w || y >= (int)m.h) return 0;
  const uint8_t *p = m.map + (size_t)y * m.pitch;
  switch (m.fmt) {
    case DRM_FORMAT_XRGB8888: case DRM_FORMAT_ARGB8888: p += x * 4; rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0]; return 1;
    case DRM_FORMAT_XBGR8888: case DRM_FORMAT_ABGR8888: p += x * 4; rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2]; return 1;
    case DRM_FORMAT_RGB888: p += x * 3; rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0]; return 1;
    case DRM_FORMAT_BGR888: p += x * 3; rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2]; return 1;
    case DRM_FORMAT_RGB565: { p += x * 2; uint16_t v = (uint16_t)(p[0] | p[1] << 8); rgb[0] = (uint8_t)((v >> 11) << 3); rgb[1] = (uint8_t)(((v >> 5) & 63) << 2); rgb[2] = (uint8_t)((v & 31) << 3); return 1; }
    default: return 0;
  }
}
inline int near_(const uint8_t *a, const uint8_t *b) { return abs(a[0] - b[0]) <= 8 && abs(a[1] - b[1]) <= 8 && abs(a[2] - b[2]) <= 8; }

// MPC's landscape screen (lw x lh) lies in the display turned by rot: 0 as is, 1 90 clockwise, 2 180, 3 90 anticlockwise
inline void ui_to_fb(int rot, int lw, int lh, int u, int v, int *x, int *y) {
  switch (rot) {
    case 0: *x = u; *y = v; break;
    case 1: *x = lh - 1 - v; *y = u; break;
    case 2: *x = lw - 1 - u; *y = lh - 1 - v; break;
    default: *x = v; *y = lw - 1 - u; break;
  }
}
inline void fb_to_ui_f(int rot, int lw, int lh, float x, float y, float *u, float *v) {
  switch (rot) {
    case 0: *u = x; *v = y; break;
    case 1: *u = y; *v = (float)lh - x; break;
    case 2: *u = (float)lw - x; *v = (float)lh - y; break;
    default: *u = (float)lw - y; *v = x; break;
  }
}
int marker_rot(const FbMap &m, int rot, int lw, int lh, int u, int v, const uint8_t mk[4][3]) {
  for (int k = 0; k < 4; k++) {
    int x, y;
    uint8_t c[3];
    ui_to_fb(rot, lw, lh, u + (k & 1), v + (k >> 1), &x, &y);
    if (!px_rgb(m, x, y, c) || !near_(c, mk[k])) return 0;
  }
  return 1;
}
int page_here(const FbMap &m, int rot, int lw, int lh, int ox, int oy) {
  return marker_rot(m, rot, lw, lh, ox, oy, MARK_TL) && marker_rot(m, rot, lw, lh, ox + PAGE_W - 2, oy + PAGE_H - 2, MARK_BR);
}
int find_page(const FbMap &m, int *rot, int *lw, int *lh, int *ox, int *oy) {
  for (int r = 0; r < 4; r++) {
    int LW = (r & 1) ? (int)m.h : (int)m.w, LH = (r & 1) ? (int)m.w : (int)m.h;
    if (LW < PAGE_W - 4 || LH < PAGE_H - 4) continue;
    for (int y0 = -2; y0 <= LH - PAGE_H + 2; y0++)
      for (int x0 = -2; x0 <= LW - PAGE_W + 2; x0++)
        if (page_here(m, r, LW, LH, x0, y0)) { *rot = r; *lw = LW; *lh = LH; *ox = x0; *oy = y0; return 1; }
  }
  return 0;
}
// 1: MANTRA's page is on the display (full: search the whole picture when it isn't where it was)
int locate(Disp &d, int full, unsigned *fbw, unsigned *fbh) {
  FbMap m;
  if (scanout_open(d, m)) { d.fmt_unknown = d.fb_err_logged > 0; return 0; }
  d.fmt_unknown = false;
  *fbw = m.w;
  *fbh = m.h;
  int on = 0;
  if (d.have_page) on = page_here(m, d.rot, d.lw, d.lh, d.box_x, d.box_y);
  if (!on && full) {
    int r, lw, lh, ox, oy;
    if (find_page(m, &r, &lw, &lh, &ox, &oy)) {
      bool moved = !d.have_page || r != d.rot || lw != d.lw || ox != d.box_x || oy != d.box_y;
      d.rot = r; d.lw = lw; d.lh = lh; d.box_x = ox; d.box_y = oy;
      d.have_page = true;
      on = 1;
      if (moved) logf_("page found: display %ux%u %.4s, MPC's %dx%d screen turned %d x 90 degrees, page at %d,%d", m.w, m.h, (char *)&m.fmt, lw, lh, r, ox, oy);
    }
  }
  scanout_close(m);
  return on;
}

// ---------------------------------------------------------------------------------------------- the touchscreen
struct Ev32 { uint32_t sec, usec; uint16_t type, code; int32_t value; };
struct Touch {
  int fd = -1, mt = 0, minx = 0, maxx = 0, miny = 0, maxy = 0, slot = 0, claimed = -1;
  struct { int down, fresh, x, y; } s[10];
  long nevents = 0, nclaims = 0;
  int size_logged = 0;
  // the finger that is drawing
  Slot *claim_slot = nullptr;
  int claim_mod = 0, prev_col = 0, prev_val = 0;
};
#define TBIT(a, n) (((a)[(n) / 8] >> ((n) % 8)) & 1)

int touch_open(Touch &t) {
  memset(t.s, 0, sizeof t.s);
  char cand[40][64];
  int nc = 0;
  snprintf(cand[nc++], 64, "/dev/input/touchscreen0");
  for (int i = 0; i < 32 && nc < 40; i++) snprintf(cand[nc++], 64, "/dev/input/event%d", i);
  for (int i = 0; i < nc; i++) {
    int fd = open(cand[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    char name[96] = "?";
    ioctl(fd, EVIOCGNAME(sizeof name), name);
    uint8_t abs_[(ABS_MAX + 8) / 8 + 1];
    memset(abs_, 0, sizeof abs_);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_), abs_);
    int mt = TBIT(abs_, ABS_MT_POSITION_X) && TBIT(abs_, ABS_MT_POSITION_Y);
    int st = TBIT(abs_, ABS_X) && TBIT(abs_, ABS_Y);
    logf_("touch: %s is \"%s\": multitouch %d, single-touch axes %d", cand[i], name, mt, st);
    if (!mt && !st) { close(fd); continue; }
    struct input_absinfo ax, ay;
    if (ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax) || ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay)) { close(fd); continue; }
    if (ax.maximum <= ax.minimum || ay.maximum <= ay.minimum) { close(fd); continue; }
    t.fd = fd; t.mt = mt;
    t.minx = ax.minimum; t.maxx = ax.maximum; t.miny = ay.minimum; t.maxy = ay.maximum;
    logf_("touch: using %s: x %d..%d, y %d..%d", cand[i], t.minx, t.maxx, t.miny, t.maxy);
    return 0;
  }
  logf_("touch: no touchscreen found (the step faders stay as the host draws them)");
  return -1;
}

// ---------------------------------------------------------------------------------------------- shared state
pthread_mutex_t g_life = PTHREAD_MUTEX_INITIALIZER;   // attach / detach (and the thread's start / join)
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   // the slot list, held while the thread talks to a slot
Slot *g_slots[16];
int g_nslots = 0;
pthread_t g_thread;
bool g_running = false;
volatile int g_stop = 0;

// the instance the user touched last, among those showing a steps page
Slot *pick_owner() {
  Slot *best = nullptr;
  uint32_t bt = 0;
  for (int i = 0; i < g_nslots; i++) {
    if (g_slots[i]->page_mod.load(std::memory_order_relaxed) <= 0) continue;
    uint32_t t = g_slots[i]->last_ui.load(std::memory_order_relaxed);
    if (!best || (int32_t)(t - bt) > 0) { best = g_slots[i]; bt = t; }
  }
  return best;
}

void release(Touch &t) {
  if (t.claim_slot) {
    t.claim_slot->up_ms.store(now_ms(), std::memory_order_relaxed);
    t.claim_slot->touching.store(0, std::memory_order_release);
    t.claim_slot = nullptr;
  }
  t.claimed = -1;
}

inline int col_of(float px) {
  int c = (int)floorf((px - (float)(mxf::ST_X0 - mxf::ST_DX / 2)) / (float)mxf::ST_DX);
  return c < 0 ? 0 : c >= mxf::ST_N ? mxf::ST_N - 1 : c;
}
inline int val_of(float py) {
  float v = 1.0f - (py - (float)(mxf::ST_Y0 + INSET)) / (float)(mxf::ST_H - 2 * INSET);
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  return (int)lrintf(v * 65535.0f);
}

// a finger at page position (px, py) of the claimed instance: the faders it crossed since the last position take their values
void draw_to(Touch &t, float px, float py, bool first) {
  int col = col_of(px), val = val_of(py);
  Slot *s = t.claim_slot;
  if (first) {
    s->push(pack(t.claim_mod, col, val));
  } else if (col == t.prev_col) {
    if (val != t.prev_val) s->push(pack(t.claim_mod, col, val));
  } else {
    int dir = col > t.prev_col ? 1 : -1, n = abs(col - t.prev_col);
    for (int k = 1; k <= n; k++) {
      int c = t.prev_col + dir * k;
      int v = t.prev_val + (int)lrintf((float)(val - t.prev_val) * (float)k / (float)n);
      s->push(pack(t.claim_mod, c, v));
    }
  }
  t.prev_col = col;
  t.prev_val = val;
}

struct View { Disp d; unsigned fbw = 0, fbh = 0; };

void touch_sync(Touch &t, View &vw, Slot *owner) {
  bool shown = owner && vw.d.on_screen && !vw.d.fmt_unknown && vw.d.have_page;
  if (t.claim_slot && (!shown || t.claim_slot != owner || owner->page_mod.load(std::memory_order_relaxed) != t.claim_mod + 1)) release(t);
  float fw = (float)(vw.fbw ? vw.fbw : 1), fh = (float)(vw.fbh ? vw.fbh : 1);
  for (int i = 0; i < 10; i++) {
    if (!t.s[i].down) {
      if (t.claimed == i) { release(t); logf_("finger up"); }
      t.s[i].fresh = 0;
      continue;
    }
    if (!shown) { t.s[i].fresh = 0; continue; }
    float fx = ((float)(t.s[i].x - t.minx) / (float)(t.maxx - t.minx)) * fw;
    float fy = ((float)(t.s[i].y - t.miny) / (float)(t.maxy - t.miny)) * fh;
    float u, v;
    fb_to_ui_f(vw.d.rot, vw.d.lw, vw.d.lh, fx, fy, &u, &v);
    float px = u - (float)vw.d.box_x, py = v - (float)vw.d.box_y;
    if (t.claimed < 0 && t.s[i].fresh) {
      t.s[i].fresh = 0;
      float x0 = (float)(mxf::ST_X0 - mxf::ST_DX / 2), x1 = (float)(mxf::ST_X0 + mxf::ST_DX * (mxf::ST_N - 1) + mxf::ST_DX / 2);
      bool inside = px >= x0 && px < x1 && py >= (float)(mxf::ST_Y0 - MARGIN_Y) && py < (float)(mxf::ST_Y0 + mxf::ST_H + MARGIN_Y);
      if (t.nevents < 80 || t.nclaims < 5)
        logf_("touch down: raw %d,%d -> display %.0f,%.0f -> page %.0f,%.0f: %s", t.s[i].x, t.s[i].y, fx, fy, px, py, inside ? "ON THE FADERS" : "outside");
      if (inside) {
        t.claimed = i;
        t.nclaims++;
        t.claim_slot = owner;
        t.claim_mod = owner->page_mod.load(std::memory_order_relaxed) - 1;
        owner->touching.store(1, std::memory_order_release);
        draw_to(t, px, py, true);
      }
    } else if (t.claimed == i) {
      draw_to(t, px, py, false);
    }
  }
}

void touch_read(Touch &t, View &vw, Slot *owner) {
  Ev32 ev[64];
  for (;;) {
    ssize_t n = read(t.fd, ev, sizeof ev);
    if (n < 0) { if (errno == EAGAIN || errno == EINTR) return; if (errno == ENODEV) { logf_("touch: device gone"); close(t.fd); t.fd = -1; } return; }
    if (n == 0) return;
    if (n % (ssize_t)sizeof(Ev32) && !t.size_logged++) logf_("touch: read %zd bytes, not a multiple of %zu", n, sizeof(Ev32));
    int cnt = (int)(n / (ssize_t)sizeof(Ev32));
    for (int k = 0; k < cnt; k++) {
      t.nevents++;
      if (ev[k].type == EV_ABS) {
        int c = ev[k].code, v = ev[k].value;
        if (t.mt) {
          if (c == ABS_MT_SLOT) t.slot = v < 0 ? 0 : v > 9 ? 9 : v;
          else if (c == ABS_MT_TRACKING_ID) { if (v < 0) t.s[t.slot].down = 0; else { t.s[t.slot].down = 1; t.s[t.slot].fresh = 1; } }
          else if (c == ABS_MT_POSITION_X) t.s[t.slot].x = v;
          else if (c == ABS_MT_POSITION_Y) t.s[t.slot].y = v;
        } else {
          if (c == ABS_X) t.s[0].x = v; else if (c == ABS_Y) t.s[0].y = v;
        }
      } else if (ev[k].type == EV_KEY && ev[k].code == BTN_TOUCH && !t.mt) {
        t.s[0].down = ev[k].value != 0;
        if (ev[k].value) t.s[0].fresh = 1;
      } else if (ev[k].type == EV_SYN && ev[k].code == SYN_REPORT) {
        touch_sync(t, vw, owner);
      }
    }
  }
}

void *thread_main(void *) {
  logf_("step touch starting");
  View vw;
  Touch t;
  bool have_drm = false;
  double next_retry = 0;
  int looks = 0, drm_tries = 0;
  uint32_t last_look = 0, last_stat = now_ms();
  Slot *prev_owner = nullptr;
  touch_open(t);
  while (!g_stop) {
    if (t.fd >= 0) {
      struct pollfd pf;
      pf.fd = t.fd; pf.events = POLLIN; pf.revents = 0;
      int r = poll(&pf, 1, 12);
      pthread_mutex_lock(&g_lock);
      Slot *owner = pick_owner();
      if (r > 0 && (pf.revents & POLLIN)) touch_read(t, vw, owner);
      pthread_mutex_unlock(&g_lock);
      if (t.fd < 0) logf_("touch: closed");
    } else {
      usleep(40000);
      if (!g_stop && now_ms() - last_stat > 5000 && t.nevents == 0) { touch_open(t); last_stat = now_ms(); }
    }
    pthread_mutex_lock(&g_lock);
    Slot *owner = pick_owner();
    if (owner != prev_owner) {
      if (t.claim_slot && t.claim_slot != owner) release(t);
      vw.d.on_screen = false;     // a look is due before a touch counts
      last_look = 0;
      prev_owner = owner;
    }
    pthread_mutex_unlock(&g_lock);
    uint32_t now = now_ms();
    if (!owner) {
      vw.d.on_screen = false;
      if (now - last_stat >= 60000) { logf_("idle: %ld touch events, %ld fingers on the faders", t.nevents, t.nclaims); last_stat = now; }
      continue;
    }
    if (!have_drm) {
      if (drm_tries < 3 && now >= next_retry) {
        have_drm = drm_find(vw.d);
        if (!have_drm) { next_retry = now + 3000; if (++drm_tries >= 3) logf_("giving up: no display read-back (touch stays the host's)"); }
      }
      continue;
    }
    if (now - last_look >= 100) {   // 10 looks a second at the display; a whole-picture search every 5th while the page isn't found
      int on = locate(vw.d, (looks++ % 5) == 0, &vw.fbw, &vw.fbh);
      if (on != (int)vw.d.on_screen) logf_("page %s", on ? "on screen" : "not on screen");
      vw.d.on_screen = on != 0;
      last_look = now;
    }
    if (now - last_stat >= 30000) {
      logf_("%ld touch events, %ld fingers on the faders, page %s", t.nevents, t.nclaims, vw.d.on_screen ? "on screen" : "not on screen");
      last_stat = now;
    }
  }
  pthread_mutex_lock(&g_lock);
  release(t);
  pthread_mutex_unlock(&g_lock);
  if (t.fd >= 0) close(t.fd);
  logf_("step touch stopped");
  return nullptr;
}

}  // namespace

uint32_t now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

void attach(Slot *s, const char *data_dir) {
  pthread_mutex_lock(&g_life);
  pthread_mutex_lock(&g_lock);
  if (g_nslots < 16) g_slots[g_nslots++] = s;
  pthread_mutex_unlock(&g_lock);
  if (!g_running && data_dir && data_dir[0] && access("/proc/self/fd", R_OK) == 0) {
    char off[700];
    snprintf(off, sizeof off, "%s/touch.off", data_dir);
    if (access(off, F_OK) != 0) {
      snprintf(g_log, sizeof g_log, "%s/touch.log", data_dir);
      FILE *f = fopen(g_log, "w");   // a fresh log each start
      if (f) fclose(f); else g_log[0] = 0;
      g_nlog = 0;
      g_stop = 0;
      if (pthread_create(&g_thread, NULL, thread_main, NULL) == 0) g_running = true;
      else logf_("no thread");
    }
  }
  pthread_mutex_unlock(&g_life);
}

void detach(Slot *s) {
  pthread_mutex_lock(&g_life);
  pthread_mutex_lock(&g_lock);
  for (int i = 0; i < g_nslots; i++)
    if (g_slots[i] == s) { g_slots[i] = g_slots[--g_nslots]; break; }
  int left = g_nslots;
  pthread_mutex_unlock(&g_lock);
  if (!left && g_running) {
    g_stop = 1;
    pthread_join(g_thread, NULL);
    g_running = false;
  }
  pthread_mutex_unlock(&g_life);
}

}  // namespace stp
