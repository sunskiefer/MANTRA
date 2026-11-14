// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// MANTRA -- rhythmic multi-effect for Force / MPC OS (mpc_engine adapter).
//
// The sound is mantra_engine.h (two layers x six effects, eight step modulators, a 24 slot matrix, master EQ / reverb).
// This file is the glue the MPC host needs:
//   * the parameter model (param_ids.h, generated from tools/model.py): knobs are normalised 0..1 (the engine applies the
//     skew), choices and whole numbers are real values, the 256 step levels are real 0..1;
//   * the pages: a `view` parameter, derived 0/1 flags that show / hide the controls of one page, Prev / Next / Overview
//     and a button per page;
//   * the 16 Q-Links, re-aimed at the controls of the current page (a tempo pair - free time / synced division - follows
//     its switch; the step pages use two banks of 16);
//   * the host's song position and tempo, handed to the modulators so a pattern always starts on the bar.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <new>

extern "C" {
#include "engine.h"
}
#include "denormals.h"
#include "mantra_engine.h"
#include "param_ids.h"
#include "factory.h"
#include "preset_io.h"
#include "step_touch.h"

namespace {
using namespace mxf;

constexpr int SR = 44100;

inline double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// ---- key lookup (sorted index, binary search) ------------------------------------------------------------------------
int g_sorted[N_PARAMS];
bool g_sorted_ok = false;
int cmp_idx(const void *a, const void *b) { return strcmp(KEYS[*(const int *)a], KEYS[*(const int *)b]); }
void build_index() {
  if (g_sorted_ok) return;
  for (int i = 0; i < N_PARAMS; i++) g_sorted[i] = i;
  qsort(g_sorted, N_PARAMS, sizeof(int), cmp_idx);
  g_sorted_ok = true;
}
int find_key(const char *k) {
  int lo = 0, hi = N_PARAMS - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(k, KEYS[g_sorted[mid]]);
    if (c == 0) return g_sorted[mid];
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return -1;
}

struct Plug {
  mx::Engine *eng = nullptr;
  double pv[N_PARAMS];          // the value of each parameter as the host sees it (units: see KIND)
  int view = 0;
  unsigned rev = 1;
  int nav_cool = 0;             // frames: a held Prev / Next must not run away
  int pr_cool = 0;              // frames: the same guard for the preset list keys (one tap can reach us as several writes)
  float tmp_l[128], tmp_r[128];
  stp::Slot tch;                // the step-fader touch reader's mailbox (src/step_touch.cc)

  void bump() { rev++; }

  // touch events from the reader thread: a step fader set where the finger is (applied here, in the audio call, so it
  // is serialised with set_param by the wrapper's lock)
  void pump_touch() {
    uint32_t e;
    int n = 0;
    while (n < 128 && tch.pop(&e)) {
      int m = stp::ev_mod(e), st = stp::ev_step(e);
      if (m >= mx::NUM_MODS) continue;
      double v = stp::ev_val(e) * (1.0 / 65535.0);
      pv[P_STEP0 + m * ST_N + st] = v;
      eng->setStep(m, st, v);
      n++;
    }
    if (n) bump();
  }

  // ---- the randomiser ---------------------------------------------------------------------------------------------------
  uint32_t rng = 0x9e3779b9u;
  uint32_t rnd32() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
  double rnd01() { return (rnd32() >> 8) * (1.0 / 16777216.0); }

  // ---- the preset manager (factory presets in factory.h, user presets as files, see preset_io.h) ----------------------------
  PresetIO io;
  UEnt *user = nullptr;           // the user's presets, sorted by name
  int nuser = 0, cuser = 0;
  int *vis = nullptr;             // the list of the current style: < 1000 a factory preset, >= 1000 user[code - 1000]
  int nvis = 0, cvis = 0;
  bool vis_dirty = true;
  char cur_name[32] = {0};        // the loaded preset
  bool cur_user = false;
  int cur_style = 0;
  char msg[40] = {0};
  bool del_armed = false;
  double ql_last[16];
  int io_pending = 0;             // SAVE / DELETE jobs posted and not answered yet
  bool io_rescan = false;
  double t_create = 0;            // taps on the preset lists are ignored for the first 2 s (a project restore sets every parameter)

  // ---- the engine value behind a parameter ----------------------------------------------------------------------------
  double real(int p) const {    // the engine-side value of a knob
    const mx::ParamInfo &pi = mx::PARAMS[ENG[p]];
    return mx::Engine::fromNorm(pi, pv[p]);
  }

  void init() {
    build_index();
    void *mem = calloc(1, sizeof(mx::Engine));
    eng = new (mem) mx::Engine();
    eng->prepare(SR);
    for (int p = 0; p < N_PARAMS; p++) pv[p] = 0;
    // defaults: same as the engine's own and the step levels at 100 %
    for (int p = 0; p < N_ENG; p++) {
      const mx::ParamInfo &pi = mx::PARAMS[p];
      switch (KIND[p]) {
        case K_CONT: pv[p] = mx::Engine::toNorm(pi, pi.def); break;
        case K_ENUM: case K_INT: pv[p] = floor(pi.def + 0.5); break;
        default: pv[p] = pi.def; break;
      }
    }
    for (int s = 0; s < N_STEP; s++) pv[P_STEP0 + s] = 1.0;
    pv[P_G_HOSTSYNC] = 1.0;
    eng->setHostSync(true);
    for (int j = 0; j < N_RND; j++) pv[P_RND0 + 2 * j + 1] = 2;     // 50 %
    for (int i = 0; i < 16; i++) ql_last[i] = 0.5;
    rng ^= (uint32_t)(uintptr_t)this * 2654435761u ^ (uint32_t)time(nullptr) * 40503u;
    if (!rng) rng = 1;
  }

  void set_view(int v) {
    if (v < 0 || v >= N_VIEWS || v == view) return;
    view = v;
    pv[P_VIEW] = v;
    tch.page_mod.store(v >= ST_VIEW1 && v < ST_VIEW1 + 8 ? v - ST_VIEW1 + 1 : 0, std::memory_order_relaxed);
    bump();
  }

  // ---- randomiser -------------------------------------------------------------------------------------------------------
  // One press: every parameter of the page moves. Continuous values (and step levels) move up or down by up to the chosen share of
  // their range (100 % = anywhere); choices and whole numbers change to a random other value with that probability. Every press is
  // a new result, there is no undo.
  void randomise(int j) {
    static const double AMT[6] = {0.1, 0.3, 0.5, 0.7, 0.9, 1.0};
    int ai = (int)pv[P_RND0 + 2 * j + 1];
    if (ai < 0) ai = 0;
    if (ai > 5) ai = 5;
    double a = AMT[ai];
    for (int n = 0; n < RND_MAX; n++) {
      int p = RND_LIST[j][n];
      if (p < 0) break;
      switch (KIND[p]) {
        case K_CONT: case K_STEP:
          set(p, a >= 0.999 ? rnd01() : clamp01(pv[p] + (rnd01() * 2.0 - 1.0) * a));
          break;
        case K_ENUM: case K_INT:
          if (rnd01() < a) {
            int lo = (int)LO[p], hi = (int)HI[p], span = hi - lo + 1;
            if (span > 1) {
              int cur = (int)pv[p];
              int nv = lo + (int)(rnd32() % (unsigned)(span - 1));
              if (nv >= cur) nv++;
              set(p, nv);
            }
          }
          break;
        default: break;
      }
    }
  }

  // ---- presets ----------------------------------------------------------------------------------------------------------
  static int ncmp(const void *a, const void *b) { return strcasecmp(((const UEnt *)a)->name, ((const UEnt *)b)->name); }
  void sort_user() { if (nuser > 1) qsort(user, nuser, sizeof(UEnt), ncmp); vis_dirty = true; }
  int find_user(const char *nm) const { for (int i = 0; i < nuser; i++) if (!strcmp(user[i].name, nm)) return i; return -1; }
  void upsert_user(const char *nm, int style) {
    int i = find_user(nm);
    if (i < 0) {
      if (nuser == cuser) { cuser = cuser ? cuser * 2 : 64; user = (UEnt *)realloc(user, sizeof(UEnt) * cuser); }
      memset(&user[nuser], 0, sizeof(UEnt));
      snprintf(user[nuser].name, sizeof user[nuser].name, "%s", nm);
      i = nuser++;
    }
    user[i].style = (uint8_t)style;
    sort_user();
  }
  void rebuild() {
    if (!vis_dirty) return;
    vis_dirty = false;
    int st = (int)pv[P_PR_STYLE];
    nvis = 0;
    int need = N_FACTORY + nuser;
    if (need > cvis) { cvis = need + 64; vis = (int *)realloc(vis, sizeof(int) * cvis); }
    if (st != N_PR_STY - 1) for (int f = 0; f < N_FACTORY; f++) if (FP_STYLE[f] == st) vis[nvis++] = f;
    for (int u = 0; u < nuser; u++) if (st == N_PR_STY - 1 || user[u].style == st) vis[nvis++] = 1000 + u;
  }
  int count() { rebuild(); return nvis; }
  const char *entry_name(int code) const { return code < 1000 ? FP_NAME[code] : user[code - 1000].name; }
  int pages() { int n = count(); return n ? (n + PR_COLS * PR_ROWS - 1) / (PR_COLS * PR_ROWS) : 1; }
  int pgi() const { return (int)pv[P_PR_PG]; }
  bool is_cur(int code) const {
    if (!cur_name[0]) return false;
    return (code >= 1000) == cur_user && !strcmp(entry_name(code), cur_name);
  }
  void clear_msg() { msg[0] = 0; del_armed = false; }
  void select(int i) {
    int n = count();
    if (i > n - 1) i = n - 1;
    if (i < 0) i = 0;
    pv[P_PR_SEL] = i;
    pv[P_PR_PG] = i / (PR_COLS * PR_ROWS);
  }
  void apply_values(const float *v) {          // a preset's values (NaN = leave the parameter as it is)
    for (int i = 0; i < N_FP; i++) if (v[i] == v[i]) set(FP_IDX[i], v[i]);
  }
  void load_index(int i) {
    int n = count();
    if (i < 0 || i >= n) return;
    clear_msg();
    select(i);
    int code = vis[i];
    if (code < 1000) {
      apply_values(FP_VAL[code]);
      snprintf(cur_name, sizeof cur_name, "%s", FP_NAME[code]);
      cur_user = false;
      cur_style = FP_STYLE[code];
    } else {
      snprintf(cur_name, sizeof cur_name, "%s", user[code - 1000].name);
      cur_user = true;
      cur_style = user[code - 1000].style;
      if (!io.post(IO_LOAD, cur_name, 0, nullptr)) snprintf(msg, sizeof msg, "BUSY");
    }
    bump();
  }
  void set_style(int st) {
    if (st < 0) st = 0;
    if (st > N_PR_STY - 1) st = N_PR_STY - 1;
    pv[P_PR_STYLE] = st;
    pv[P_PR_SEL] = 0;
    pv[P_PR_PG] = 0;
    vis_dirty = true;
    clear_msg();
    bump();
  }
  void next_name(char *out, size_t n) const {
    int mx = 0;
    for (int i = 0; i < nuser; i++) {
      const char *c = user[i].name;
      if (!strncmp(c, "User ", 5) && c[5] >= '0' && c[5] <= '9') { int v = atoi(c + 5); if (v > mx) mx = v; }
    }
    snprintf(out, n, "User %03d", mx + 1);
  }
  void save(bool as_new) {
    clear_msg();
    if (!io.enabled()) { snprintf(msg, sizeof msg, "NO PRESET FOLDER"); bump(); return; }
    float vals[N_FP];
    for (int i = 0; i < N_FP; i++) vals[i] = (float)pv[FP_IDX[i]];
    char name[32];
    int style;
    if (!as_new && cur_user && cur_name[0]) { snprintf(name, sizeof name, "%s", cur_name); style = cur_style; }
    else { next_name(name, sizeof name); style = (int)pv[P_PR_STYLE]; }
    if (!io.post(IO_SAVE, name, style, vals)) { snprintf(msg, sizeof msg, "BUSY, TRY AGAIN"); bump(); return; }
    io_pending++;
    upsert_user(name, style);
    snprintf(cur_name, sizeof cur_name, "%s", name);
    cur_user = true;
    cur_style = style;
    rebuild();
    for (int i = 0; i < nvis; i++) if (is_cur(vis[i])) { select(i); break; }
    snprintf(msg, sizeof msg, "SAVED");
    bump();
  }
  void del() {
    if (!cur_user || !cur_name[0]) { clear_msg(); snprintf(msg, sizeof msg, "ONLY YOUR OWN PRESETS"); bump(); return; }
    if (!del_armed) { clear_msg(); del_armed = true; snprintf(msg, sizeof msg, "TAP DELETE AGAIN"); bump(); return; }
    int u = find_user(cur_name);
    if (io.post(IO_DELETE, cur_name, 0, nullptr)) io_pending++;
    if (u >= 0) { for (int i = u + 1; i < nuser; i++) user[i - 1] = user[i]; nuser--; }
    cur_name[0] = 0;
    cur_user = false;
    vis_dirty = true;
    clear_msg();
    snprintf(msg, sizeof msg, "DELETED");
    select((int)pv[P_PR_SEL]);
    bump();
  }
  void page_step(int d) {
    clear_msg();
    int np = pages(), pg = pgi() + d;
    if (pg < 0) pg = 0;
    if (pg > np - 1) pg = np - 1;
    pv[P_PR_PG] = pg;
    bump();
  }
  // a tap on one of the list rows (a preset or a style)
  void row_tap(int p) {
    if (p >= P_PR_STY_1 && p < P_PR_STY_1 + N_PR_STY) { set_style(p - P_PR_STY_1); return; }
    load_index(pgi() * PR_COLS * PR_ROWS + (p - P_PR_ROW_1));
  }
  // Q-Link 2 on the PRESETS page: one preset per click (a big jump goes straight there), loading it
  void ql_preset(int n, double v) {
    int cnt = count();
    if (cnt <= 0) return;
    double d = v - ql_last[n];
    ql_last[n] = v;
    int i = (int)pv[P_PR_SEL];
    if (d > 0.1 || d < -0.1) i = (int)lround(v * (cnt - 1));
    else if (d > 0) i++;
    else if (d < 0) i--;
    else return;
    if (i < 0) i = 0;
    if (i > cnt - 1) i = cnt - 1;
    load_index(i);
  }
  void pump_io() {
    IoRes r;
    for (int g = 0; g < 4 && io.poll(r); g++) {
      if (r.type == IO_SAVE || r.type == IO_DELETE) {
        if (io_pending > 0) io_pending--;
        if (io_pending == 0 && io_rescan) { io_rescan = false; io.post(IO_SCAN, "", 0, nullptr); }
      }
      if (r.type == IO_SCAN && r.ok && io_pending > 0) {     // a scan that ran before our own writes: the local list is newer
        io_rescan = true;
        free(r.list);
      } else if (r.type == IO_SCAN && r.ok) {
        free(user);
        user = r.list; nuser = r.n; cuser = r.n;
        if (!user) { cuser = 0; nuser = 0; }
        sort_user();
        if (cur_user && cur_name[0]) { int u = find_user(cur_name); if (u >= 0) cur_style = user[u].style; }
        bump();
      } else if (r.type == IO_LOAD) {
        if (r.ok && r.vals) apply_values(r.vals); else snprintf(msg, sizeof msg, "LOAD FAILED");
        free(r.vals);
        bump();
      } else if (r.type == IO_SAVE && !r.ok) {
        snprintf(msg, sizeof msg, "SAVE FAILED");
        io.post(IO_SCAN, "", 0, nullptr);
        bump();
      } else { free(r.vals); free(r.list); }
    }
  }
  const char *title_text(int p, char *b, int n) {
    if (p == P_PAGE_TITLE) { snprintf(b, n, "%s", VIEW_TITLE[view]); return b; }
    if (p == P_PR_CUR) { snprintf(b, n, "%s%s", cur_user ? "* " : "", cur_name[0] ? cur_name : "-"); return b; }
    if (p == P_PR_INFO) {
      if (msg[0]) snprintf(b, n, "%s", msg); else snprintf(b, n, "%d / %d", pgi() + 1, pages());
      return b;
    }
    b[0] = 0;
    return b;
  }
  void row_text(int p, char *b, int n) {
    if (p >= P_PR_STY_1 && p < P_PR_STY_1 + N_PR_STY) { snprintf(b, n, "%s", PR_STYLE_NAME[p - P_PR_STY_1]); return; }
    int i = pgi() * PR_COLS * PR_ROWS + (p - P_PR_ROW_1);
    if (i >= count()) { snprintf(b, n, " "); return; }
    int code = vis[i];
    snprintf(b, n, "%s%s", code >= 1000 ? "* " : "", entry_name(code));
  }
  double row_value(int p) {
    if (p >= P_PR_STY_1 && p < P_PR_STY_1 + N_PR_STY) return (int)pv[P_PR_STYLE] == p - P_PR_STY_1 ? 1 : 0;
    int i = pgi() * PR_COLS * PR_ROWS + (p - P_PR_ROW_1);
    if (i >= count()) return 0;
    return is_cur(vis[i]) ? 1 : 0;
  }

  // ---- write a parameter ------------------------------------------------------------------------------------------------
  void set(int p, double v) {
    switch (KIND[p]) {
      case K_CONT: {
        double n = clamp01(v);
        pv[p] = n;
        eng->setParam(ENG[p], mx::Engine::fromNorm(mx::PARAMS[ENG[p]], n));
        break;
      }
      case K_ENUM: {
        double i = floor(v + 0.5);
        if (i < LO[p]) i = LO[p];
        if (i > HI[p]) i = HI[p];
        pv[p] = i;
        eng->setParam(ENG[p], i);
        break;
      }
      case K_INT: {
        double i = floor(v + 0.5);
        if (i < LO[p]) i = LO[p];
        if (i > HI[p]) i = HI[p];
        pv[p] = i;
        eng->setParam(ENG[p], i);
        break;
      }
      case K_STEP: {
        double n = clamp01(v);
        pv[p] = n;
        int k = p - P_STEP0;
        eng->setStep(k / mxf_nstep(), k % mxf_nstep(), n);
        break;
      }
      case K_HOST: pv[p] = v > 0.5 ? 1 : 0; eng->setHostSync(v > 0.5); break;
      case K_BANK: pv[p] = v > 0.5 ? 1 : 0; break;
      case K_SEL: {
        double i = floor(v + 0.5);
        if (i < LO[p]) i = LO[p];
        if (i > HI[p]) i = HI[p];
        if (p == P_PR_STYLE) { if ((int)i != (int)pv[p]) set_style((int)i); return; }
        pv[p] = i;
        break;
      }
      default: return;
    }
    bump();
  }
  static int mxf_nstep() { return mx::NUM_STEPS; }

  // ---- the Q-Link target of slot n on the current page ------------------------------------------------------------------
  int ql_target(int n) const {
    const QlSlot &q = VIEW_QL[view][pv[P_STEPBANK] > 0.5 ? 1 : 0][n];
    if (q.a < 0) return -1;
    if (q.b >= 0 && q.sel >= 0 && pv[q.sel] > 0.5) return q.b;
    return q.a;
  }
  double norm_of(int t) const {   // a parameter as 0..1 (what a Q-Link shows)
    switch (KIND[t]) {
      case K_CONT: case K_STEP: return pv[t];
      case K_ENUM: case K_INT: { double span = HI[t] - LO[t]; return span > 0 ? (pv[t] - LO[t]) / span : 0; }
      case K_SEL:
        if (t == P_PR_SEL) { int c = const_cast<Plug *>(this)->count(); return c > 1 ? pv[t] / (c - 1) : 0; }
        { double span = HI[t] - LO[t]; return span > 0 ? (pv[t] - LO[t]) / span : 0; }
      default: return 0;
    }
  }
  void set_norm(int t, double n) {
    n = clamp01(n);
    switch (KIND[t]) {
      case K_CONT: case K_STEP: set(t, n); break;
      case K_ENUM: case K_INT: set(t, LO[t] + n * (HI[t] - LO[t])); break;
      case K_SEL: set(t, LO[t] + n * (HI[t] - LO[t])); break;
      default: break;
    }
  }

  // ---- text -----------------------------------------------------------------------------------------------------------
  static void hz_text(double hz, char *b, int n) {
    if (hz >= 1000.0) snprintf(b, n, "%.2f kHz", hz / 1000.0); else snprintf(b, n, "%d Hz", (int)lround(hz));
  }
  void display(int p, char *buf, int n) const {
    int k = KIND[p];
    if (k == K_ENUM) {
      int i = (int)pv[p];
      const EnumDef &e = ENUMS[ENUM_OF[p]];
      if (i < 0) i = 0;
      if (i >= e.n) i = e.n - 1;
      snprintf(buf, n, "%s", e.labels[i]);
      return;
    }
    if (k == K_STEP) { snprintf(buf, n, "%d %%", (int)lround(pv[p] * 100.0)); return; }
    if (k == K_SEL) {
      if (ENUM_OF[p] >= 0) {
        const EnumDef &e = ENUMS[ENUM_OF[p]];
        int i = (int)pv[p];
        if (i < 0) i = 0;
        if (i >= e.n) i = e.n - 1;
        snprintf(buf, n, "%s", e.labels[i]);
      } else if (p == P_PR_SEL) {
        Plug *self = const_cast<Plug *>(this);
        int c = self->count(), i = (int)pv[p];
        if (i >= c) i = c - 1;
        if (i < 0) snprintf(buf, n, "-"); else snprintf(buf, n, "%s", self->entry_name(self->vis[i]));
      } else snprintf(buf, n, "%d", (int)pv[p]);
      return;
    }
    if (k == K_ROW) { const_cast<Plug *>(this)->row_text(p, buf, n); return; }
    if (k == K_TITLE) { const_cast<Plug *>(this)->title_text(p, buf, n); return; }
    if (k == K_INT) {
      int i = (int)pv[p];
      if (FMT[p] == F_DEST) {
        if (i < 0) i = 0;
        if (i > 84) i = 84;
        snprintf(buf, n, "%s", DEST_SHORT[i]);
      } else snprintf(buf, n, "%d", i);
      return;
    }
    if (k != K_CONT) { buf[0] = 0; return; }
    double r = real(p);
    switch (FMT[p]) {
      case F_PCT: snprintf(buf, n, "%d %%", (int)lround(r * 100.0)); break;
      case F_SPCT: snprintf(buf, n, "%+d %%", (int)lround(r * 100.0)); break;
      case F_PAN: {
        int c = (int)lround(r * 100.0);
        if (c == 0) snprintf(buf, n, "C"); else snprintf(buf, n, "%c %d", c < 0 ? 'L' : 'R', c < 0 ? -c : c);
        break;
      }
      case F_HZ: hz_text(r, buf, n); break;
      case F_HZLFO: snprintf(buf, n, "%.2f Hz", r); break;
      case F_DB: snprintf(buf, n, "%+.1f dB", r); break;
      case F_MS: if (r < 10.0) snprintf(buf, n, "%.1f ms", r); else snprintf(buf, n, "%d ms", (int)lround(r)); break;
      case F_NUM: snprintf(buf, n, "%.2f", r); break;
      case F_BIT: snprintf(buf, n, "%.1f bit", r); break;
      case F_MULT: snprintf(buf, n, "x%.2f", r); break;
      case F_BPM:
        if (pv[P_G_HOSTSYNC] > 0.5) snprintf(buf, n, "HOST"); else snprintf(buf, n, "%d BPM", (int)lround(r));
        break;
      default: snprintf(buf, n, "%.2f", r); break;
    }
  }

  // ---- derived values -------------------------------------------------------------------------------------------------
  double flag(int p) const {
    if (p == P_IN_SEC) return view > 0;
    const FlagDef &f = FLAGS[p - P_FLAG0];
    if (f.sel < 0) return view == f.page;
    return view == f.page && ((pv[f.sel] > 0.5) == (f.mode != 0));
  }
};

// ---- mpc_engine glue ------------------------------------------------------------------------------------------------
double now_s() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

void *create(const char *data_dir) {
  void *mem = calloc(1, sizeof(Plug));
  if (!mem) return nullptr;
  Plug *s = new (mem) Plug();
  s->init();
  s->t_create = now_s();
  s->io.start(data_dir, KEYS, FP_IDX, N_FP, find_key, PR_STYLE_NAME, N_PR_STY);
  s->io.post(IO_SCAN, "", 0, nullptr);
  stp::attach(&s->tch, data_dir);
  return s;
}

void destroy(void *inst) {
  Plug *s = (Plug *)inst;
  stp::detach(&s->tch);
  s->io.stop();
  free(s->user);
  free(s->vis);
  if (s->eng) { s->eng->~Engine(); free(s->eng); }
  s->~Plug();
  free(s);
}

void midi(void *, const uint8_t *, int) {}

void trigger(Plug *s, int p) {
  if (p == P_NAV_ZOOM) { s->set_view(0); return; }
  if (p == P_NAV_PREV || p == P_NAV_NEXT) {
    if (s->nav_cool > 0) return;
    s->nav_cool = SR / 4;
    int v = s->view <= 0 ? (p == P_NAV_NEXT ? 1 : N_VIEWS - 1) : s->view + (p == P_NAV_NEXT ? 1 : -1);   // every page in order, wrapping
    if (v < 1) v = N_VIEWS - 1;
    if (v >= N_VIEWS) v = 1;
    s->set_view(v);
    return;
  }
  if (p >= P_GO1 && p < P_GO1 + (N_VIEWS - 1)) s->set_view(1 + p - P_GO1);
}

void set_param(void *inst, const char *key, const char *val) {
  Plug *s = (Plug *)inst;
  double v = atof(val);
  int p = find_key(key);
  if (p < 0) return;
  if (now_s() - s->t_create > 2.0) s->tch.last_ui.store(stp::now_ms(), std::memory_order_relaxed);   // the user is at this instance
  if (KIND[p] == K_STEP && (s->tch.touching.load(std::memory_order_acquire) || stp::now_ms() - s->tch.up_ms.load(std::memory_order_relaxed) < 200)) return;   // a finger owns the faders
  if (p == P_VIEW) { s->set_view((int)lround(v)); return; }
  if (p >= P_QL1 && p < P_QL1 + 16) {
    int t = s->ql_target(p - P_QL1);
    if (t == P_PR_SEL) s->ql_preset(p - P_QL1, v);
    else if (t >= 0) s->set_norm(t, v);
    return;
  }
  if (p >= P_RND0 && p < P_RND0 + 2 * N_RND) {
    if (((p - P_RND0) & 1) == 0) { if (v >= 0.5) s->randomise((p - P_RND0) / 2); }
    else s->set(p, v);
    return;
  }
  if (p >= P_PR_PREV && p <= P_PR_DEL) {
    if (v < 0.5) return;
    if (s->pr_cool > 0) return;          // a tap arrives as a burst of writes: only the first one counts
    s->pr_cool = SR / 4;
    if (p == P_PR_PREV) s->page_step(-1);
    else if (p == P_PR_NEXT) s->page_step(1);
    else if (p == P_PR_SAVE) s->save(false);
    else if (p == P_PR_NEW) s->save(true);
    else s->del();
    return;
  }
  if ((p >= P_PR_STY_1 && p < P_PR_STY_1 + N_PR_STY) || (p >= P_PR_ROW_1 && p < P_PR_ROW_1 + N_PR_ROW)) {
    if (v >= 0.5 && now_s() - s->t_create > 2.0) s->row_tap(p);
    return;
  }
  if (p >= P_NAV_ZOOM && p <= P_NAV_NEXT) { if (v >= 0.5) trigger(s, p); return; }
  if (p >= P_GO1 && p < P_GO1 + (N_VIEWS - 1)) { if (v >= 0.5) trigger(s, p); return; }
  int k = KIND[p];
  if (k == K_CONT || k == K_ENUM || k == K_INT || k == K_STEP || k == K_HOST || k == K_SEL) { s->set(p, v); return; }
  if (k == K_BANK) { s->set(p, v); return; }
  // captions, flags, the title: read only
}

int get_param(void *inst, const char *key, char *buf, int n) {
  Plug *s = (Plug *)inst;
  if (!strcmp(key, "display_rev")) return snprintf(buf, n, "%u", s->rev);
  size_t kl = strlen(key);
  char base[64];
  int suffix = 0;   // 1 = _display, 2 = _name, 3 = _on (list row selected)
  if (kl > 8 && !strcmp(key + kl - 8, "_display")) { suffix = 1; kl -= 8; }
  else if (kl > 5 && !strcmp(key + kl - 5, "_name")) { suffix = 2; kl -= 5; }
  else if (kl > 3 && !strcmp(key + kl - 3, "_on")) { suffix = 3; kl -= 3; }
  if (kl >= sizeof base) return 0;
  memcpy(base, key, kl);
  base[kl] = 0;
  int p = find_key(base);
  if (p < 0) return 0;
  if (p >= P_QL1 && p < P_QL1 + 16) {
    int t = s->ql_target(p - P_QL1);
    if (suffix == 2) return snprintf(buf, n, "%s", t >= 0 ? NAMES[t] : "-");
    if (suffix == 1) { if (t < 0) return snprintf(buf, n, " "); s->display(t, buf, n); return (int)strlen(buf); }
    return snprintf(buf, n, "%g", t >= 0 ? s->norm_of(t) : 0.0);
  }
  if (suffix == 3) {
    if (KIND[p] != K_ROW) return 0;
    return snprintf(buf, n, "%d", s->row_value(p) > 0.5 ? 1 : 0);
  }
  if (suffix == 2) return 0;
  if (suffix == 1) { s->display(p, buf, n); return buf[0] ? (int)strlen(buf) : snprintf(buf, n, " "); }
  switch (KIND[p]) {
    case K_CAP: return snprintf(buf, n, "%s", CAP_TEXT[p - P_CAP0]);
    case K_TITLE: s->title_text(p, buf, n); return buf[0] ? (int)strlen(buf) : snprintf(buf, n, " ");
    case K_ROW: s->row_text(p, buf, n); return (int)strlen(buf);
    case K_SEL: return snprintf(buf, n, "%d", (int)s->pv[p]);
    case K_FLAG: return snprintf(buf, n, "%d", s->flag(p) > 0.5 ? 1 : 0);
    case K_ENUM: case K_INT: case K_HOST: case K_BANK: return snprintf(buf, n, "%d", (int)s->pv[p]);
    case K_VIEW: return snprintf(buf, n, "%d", s->view);
    case K_CONT: case K_STEP: return snprintf(buf, n, "%g", s->pv[p]);
    case K_HIDDEN: return snprintf(buf, n, "0");
    default: return snprintf(buf, n, "0");   // triggers read back as 0
  }
}

void process_fn(void *inst, const int16_t *in, int16_t *out, int frames) {
  Plug *s = (Plug *)inst;
  ScopedDenormalDisable ftz;
  if (frames > 128) frames = 128;
  const float k = 1.0f / 32768.0f;
  for (int i = 0; i < frames; i++) { s->tmp_l[i] = in[2 * i] * k; s->tmp_r[i] = in[2 * i + 1] * k; }
  s->eng->process(s->tmp_l, s->tmp_r, frames);
  for (int i = 0; i < frames; i++) {
    float l = s->tmp_l[i] * 32768.0f, r = s->tmp_r[i] * 32768.0f;
    if (!(l == l)) l = 0;
    if (!(r == r)) r = 0;
    out[2 * i] = l >= 32767.0f ? 32767 : l <= -32768.0f ? -32768 : (int16_t)lrintf(l);
    out[2 * i + 1] = r >= 32767.0f ? 32767 : r <= -32768.0f ? -32768 : (int16_t)lrintf(r);
  }
  if (s->nav_cool > 0) s->nav_cool -= frames;
  if (s->pr_cool > 0) s->pr_cool -= frames;
  s->pump_io();
  s->pump_touch();
}

void render_fn(void *, int16_t *out, int frames) { memset(out, 0, (size_t)frames * 4); }

void song_pos_fn(void *inst, double ppq, double bpm, int playing, int) {
  Plug *s = (Plug *)inst;
  s->eng->setHost(ppq, bpm, playing != 0);
}

const mpc_engine_t API = {create, destroy, midi, set_param, get_param, render_fn, process_fn, song_pos_fn};

}  // namespace

extern "C" const mpc_engine_t *mpc_engine(void) { return &API; }
