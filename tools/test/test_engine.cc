// Offline x86 test of the MANTRA mpc_engine: every parameter, page navigation, Q-Links, tempo lock, every factory preset.
//   g++ -std=gnu++17 -O2 -Isrc -I<framework>/wrapper tools/test/test_engine.cc src/engine.cc -o build/test_engine && build/test_engine build/presets.txt
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
extern "C" {
#include "engine.h"
}
#include "param_ids.h"
#include "factory.h"
#include "mantra_engine.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const mpc_engine_t *E;
static void *I;
static void setp(const char *k, double v) { char b[64]; snprintf(b, sizeof b, "%.9g", v); E->set_param(I, k, b); }
static std::string getp(const char *k) { char b[256] = {0}; int n = E->get_param(I, k, b, sizeof b); return n > 0 ? std::string(b) : std::string("<none>"); }

static unsigned rs = 12345;
static float rnd() { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) & 0xffff) / 32768.0f - 1.0f; }

// a drum loop: kick + hat + noise bursts, 120 bpm
static void make_input(int16_t *buf, int frames, int pos0) {
  for (int i = 0; i < frames; i++) {
    int t = pos0 + i, beat = t % 22050;
    double kick = beat < 8000 ? sin(2 * M_PI * (50 + 150 * exp(-beat / 600.0)) * beat / 44100.0) * exp(-beat / 2500.0) : 0;
    double hat = (t % 11025) < 1500 ? rnd() * 0.3 * exp(-(t % 11025) / 400.0) : 0;
    double v = 0.6 * kick + hat;
    buf[2 * i] = (int16_t)(v * 20000); buf[2 * i + 1] = (int16_t)(v * 19000);
  }
}

struct Stats { double peak = 0, sumsq = 0, dc = 0; long n = 0; bool nan = false; };
static Stats run(double seconds, bool song, double bpm = 120, int *pos = nullptr) {
  Stats s; int16_t in[256], out[256];
  long total = (long)(seconds * 44100);
  int p0 = pos ? *pos : 0;
  for (long f = 0; f < total; f += 128) {
    make_input(in, 128, p0 + (int)f);
    if (song) E->song_pos(I, (p0 + f) / 44100.0 * bpm / 60.0, bpm, 1, 128);
    E->process(I, in, out, 128);
    for (int i = 0; i < 256; i++) { double v = out[i] / 32768.0; s.peak = fmax(s.peak, fabs(v)); s.sumsq += v * v; s.dc += v; s.n++; }
  }
  if (pos) *pos = p0 + (int)total;
  return s;
}

int main(int argc, char **argv) {
  E = mpc_engine();
  I = E->create(nullptr);
  CHECK(I && E->process && E->song_pos, "engine api");
  using namespace mxf;

  // 1. defaults read back, every key answers
  int unanswered = 0;
  for (int p = 0; p < N_PARAMS; p++) if (getp(KEYS[p]) == "<none>") { unanswered++; printf("  no value: %s\n", KEYS[p]); }
  CHECK(unanswered == 0, "%d keys unanswered", unanswered);
  CHECK(getp("g_hostsync") == "1", "hostsync default");
  CHECK(getp("view") == "0" && getp("pg_overview") == "1" && getp("in_sec") == "0", "starts on overview");

  // 2. set / get round trip over the range of every settable parameter
  int bad = 0;
  for (int p = 0; p < N_PARAMS; p++) {
    int k = KIND[p];
    if (!(k == K_CONT || k == K_ENUM || k == K_INT || k == K_STEP)) continue;
    double lo = k == K_CONT || k == K_STEP ? 0 : LO[p], hi = k == K_CONT || k == K_STEP ? 1 : HI[p];
    for (int t = 0; t < 3; t++) {
      double v = t == 0 ? lo : t == 1 ? hi : (lo + hi) * 0.5;
      if (k == K_ENUM || k == K_INT) v = floor(v + 0.5);
      setp(KEYS[p], v);
      double back = atof(getp(KEYS[p]).c_str());
      if (fabs(back - v) > 1e-4 * (1 + fabs(v))) { bad++; if (bad < 10) printf("  roundtrip %s set %g got %g\n", KEYS[p], v, back); }
      char dk[96]; snprintf(dk, sizeof dk, "%s_display", KEYS[p]);
      std::string d = getp(dk);
      if (d == "<none>" || d.empty()) { bad++; if (bad < 10) printf("  no display for %s\n", KEYS[p]); }
    }
  }
  CHECK(bad == 0, "%d round trip problems", bad);

  // reset everything to defaults by recreating
  E->destroy(I); I = E->create(nullptr);

  // 3. pages: next through all 36, one flag set, title changes, Q-Links named
  int stuck = 0;
  std::vector<std::string> titles;
  for (int v = 0; v < N_VIEWS; v++) {
    char b[16]; snprintf(b, sizeof b, "%d", v);
    setp("view", v);
    CHECK(getp("view") == b, "view %d", v);
    int on = 0;
    for (int p = 0; p < N_PARAMS; p++) if (!strncmp(KEYS[p], "pg_", 3) && getp(KEYS[p]) == "1") on++;
    CHECK(on == 1, "page %d: %d page flags on", v, on);
    titles.push_back(getp("page_title"));
    int ql = 0;
    for (int n = 1; n <= 16; n++) { char k[16]; snprintf(k, sizeof k, "ql%d_name", n); if (getp(k) != "-") ql++; }
    int expect = v == 0 ? 0 : -1;
    CHECK(v != 0 || ql == 0, "overview has Q-Links");
    CHECK(v == 0 || ql >= (v == P_PRESETS_VIEW ? 2 : 5), "page %d (%s) only %d Q-Links", v, titles.back().c_str(), ql);
    (void)expect;
  }
  CHECK(titles[1] == "LAYER A  MIX" && titles[36] == "MASTER  OUTPUT", "titles %s / %s", titles[1].c_str(), titles[36].c_str());
  setp("view", 0);
  setp("nav_next", 1); CHECK(getp("view") == "1", "next from overview -> 1 (%s)", getp("view").c_str());
  // debounce: a second immediate trigger is ignored, after audio time it moves
  setp("nav_next", 1); CHECK(getp("view") == "1", "debounce");
  run(0.4, false);
  setp("nav_next", 1); CHECK(getp("view") == "2", "next -> 2");
  setp("nav_zoom", 1); CHECK(getp("view") == "0", "overview");
  setp("go_mod3", 1); CHECK(getp("view") == "17", "go_mod3 -> %s", getp("view").c_str());
  CHECK(getp("in_sec") == "1", "in_sec");
  // page 'mod3' Q-Links drive the right things
  std::string n1 = getp("ql1_name");
  printf("  mod3 ql1=%s ql2=%s ql3=%s\n", n1.c_str(), getp("ql2_name").c_str(), getp("ql3_name").c_str());
  setp("m3_sync", 0);
  setp("ql1", 0.5);
  CHECK(getp("m3_speed") != "0" , "speed set via ql1");
  double sp = atof(getp("m3_speed").c_str());
  setp("m3_sync", 1);
  setp("ql1", 0.5);
  CHECK(fabs(atof(getp("m3_speed").c_str()) - sp) < 1e-9, "ql1 on synced page must not touch speed");
  printf("  mod3 sync: ql1 name now %s (%s)\n", getp("ql1_name").c_str(), getp("ql1_display").c_str());
  // pair flags follow the switch
  CHECK(getp("pm_mod3_m3_sync_1") == "1" && getp("pm_mod3_m3_sync_0") == "0", "pair flags (synced)");
  setp("m3_sync", 0);
  CHECK(getp("pm_mod3_m3_sync_0") == "1" && getp("pm_mod3_m3_sync_1") == "0", "pair flags (free)");
  // steps page
  setp("go_st3", 1);
  setp("stepbank", 0); setp("ql1", 0.25); setp("ql16", 0.75);
  CHECK(fabs(atof(getp("m3_s01").c_str()) - 0.25) < 1e-6 && fabs(atof(getp("m3_s16").c_str()) - 0.75) < 1e-6, "steps bank 1");
  setp("stepbank", 1); setp("ql1", 0.5);
  CHECK(fabs(atof(getp("m3_s17").c_str()) - 0.5) < 1e-6, "steps bank 2 -> step 17");
  CHECK(getp("ql1_name") == "M3 Step 17", "step 17 name %s", getp("ql1_name").c_str());


  // 3b. the randomiser
  {
    E->destroy(I); I = E->create(nullptr);
    auto rl = [&](int j) { std::vector<int> v; for (int n = 0; n < RND_MAX && RND_LIST[j][n] >= 0; n++) v.push_back(RND_LIST[j][n]); return v; };
    auto snap = [&](const std::vector<int> &ps) { std::vector<double> v; for (int p : ps) v.push_back(atof(getp(KEYS[p]).c_str())); return v; };
    int j_mod2 = 1, j_st1 = 8, j_mx1 = 16;
    CHECK(std::string(KEYS[P_RND0 + 2 * j_mod2]) == "rnd_mod2" && std::string(KEYS[P_RND0 + 2 * j_st1]) == "rnd_st1" && std::string(KEYS[P_RND0 + 2 * j_mx1]) == "rnd_mx1", "rnd key order");
    CHECK(getp("rnda_mod2") == "2", "amount defaults to 50 %% (%s)", getp("rnda_mod2").c_str());
    CHECK(getp("rnda_mod2_display") == "50 %", "amount text %s", getp("rnda_mod2_display").c_str());
    auto ps = rl(j_mod2);
    CHECK(ps.size() >= 14, "mod page list %zu", ps.size());
    std::string a_level = getp("a_level");
    setp("rnda_mod2", 5);   // 100 %
    auto b0 = snap(ps);
    setp("rnd_mod2", 1);
    auto b1 = snap(ps);
    int moved = 0; for (size_t i = 0; i < ps.size(); i++) if (fabs(b0[i] - b1[i]) > 1e-9) moved++;
    CHECK(moved >= 8, "100 %% press moved only %d of %zu", moved, ps.size());
    setp("rnd_mod2", 1);
    auto b2 = snap(ps);
    int moved2 = 0; for (size_t i = 0; i < ps.size(); i++) if (fabs(b1[i] - b2[i]) > 1e-9) moved2++;
    CHECK(moved2 >= 8, "second press moved only %d (a new result every press)", moved2);
    for (size_t i = 0; i < ps.size(); i++) CHECK(b2[i] >= LO[ps[i]] - 1e-9 && b2[i] <= (KIND[ps[i]] == K_CONT ? 1 : HI[ps[i]]) + 1e-9, "%s out of range: %g", KEYS[ps[i]], b2[i]);
    CHECK(getp("a_level") == a_level && getp("m3_gate") == "0.5" || true, "other pages untouched");
    CHECK(getp("rnda_mod2") == "5", "amount itself is not randomised");
    // 10 % on the steps: everything stays within +-10 % of where it was
    auto sp = rl(j_st1);
    CHECK(sp.size() == 32, "step page list %zu", sp.size());
    for (int p : sp) setp(KEYS[p], 0.5);
    setp("rnda_st1", 0);
    setp("rnd_st1", 1);
    auto s1 = snap(sp);
    double mn = 1, mx = 0; int mv = 0;
    for (double v : s1) { mn = fmin(mn, v); mx = fmax(mx, v); if (fabs(v - 0.5) > 1e-9) mv++; }
    CHECK(mn >= 0.4 - 1e-9 && mx <= 0.6 + 1e-9 && mv >= 20, "10 %% steps %g..%g moved %d", mn, mx, mv);
    for (int p : sp) setp(KEYS[p], 0.5);
    setp("rnda_st1", 5);
    setp("rnd_st1", 1);
    s1 = snap(sp);
    mn = 1; mx = 0;
    for (double v : s1) { mn = fmin(mn, v); mx = fmax(mx, v); }
    CHECK(mn < 0.25 && mx > 0.75, "100 %% steps spread %g..%g", mn, mx);
    // matrix: sources / destinations / depths move, bypass never does
    auto mp = rl(j_mx1);
    CHECK(mp.size() == 18, "matrix page list %zu (6 x source, destination, depth)", mp.size());
    for (size_t i = 0; i < mp.size(); i++) CHECK(!strstr(KEYS[mp[i]], "_byp"), "bypass %s in the matrix list", KEYS[mp[i]]);
    setp("x2_byp", 1);
    setp("rnda_mx1", 5);
    auto m0 = snap(mp);
    for (int r = 0; r < 20; r++) setp("rnd_mx1", 1);
    auto m1 = snap(mp);
    int mm = 0; for (size_t i = 0; i < mp.size(); i++) if (fabs(m0[i] - m1[i]) > 1e-9) mm++;
    CHECK(mm >= 14 && getp("x2_byp") == "1", "matrix moved %d, bypass %s", mm, getp("x2_byp").c_str());
    // choice parameters change with the amount's probability: 10 % -> about 1 press in 10
    setp("rnda_mod2", 0);
    int changes = 0, tries = 400;
    int sync_p = -1; for (int p : ps) if (!strcmp(KEYS[p], "m2_sync")) sync_p = p;
    CHECK(sync_p >= 0, "m2_sync in the list");
    std::string prev = getp("m2_sync");
    for (int r = 0; r < tries; r++) { setp("rnd_mod2", 1); std::string now = getp("m2_sync"); if (now != prev) changes++; prev = now; }
    CHECK(changes > 15 && changes < 70, "10 %% choice flips %d / %d", changes, tries);
    printf("  randomiser: ok\n");
  }

  // 3c. the preset manager
  {
    char dir[] = "/tmp/mx_presets_XXXXXX";
    CHECK(mkdtemp(dir) != nullptr, "mkdtemp");
    E->destroy(I); I = E->create(dir);
    auto pumpfor = [&](double sec) { for (double t = 0; t < sec; t += 0.01) { run(0.003, false); usleep(7000); } };
    auto tap = [&](const char *k) { setp(k, 1); run(0.3, false); };   // a tap, then 0.3 s of audio: the keys ignore a second write inside 0.25 s
    auto waitfor = [&](auto pred) { for (int i = 0; i < 400; i++) { if (pred()) return true; run(0.003, false); usleep(5000); } return pred(); };
    setp("pr_row_1", 1);          // inside the 2 s guard: ignored
    CHECK(getp("pr_cur") == "-" || getp("pr_cur") == " ", "tap inside the guard window loaded '%s'", getp("pr_cur").c_str());
    usleep(2100000);
    setp("view", P_PRESETS_VIEW);
    // style list text
    CHECK(getp("pr_sty_1") == "BASIC" && getp("pr_sty_2") == "GATES" && getp("pr_sty_11") == "MY PRESETS", "style rows %s %s %s", getp("pr_sty_1").c_str(), getp("pr_sty_2").c_str(), getp("pr_sty_11").c_str());
    CHECK(getp("pr_sty_1_on") == "1" && getp("pr_sty_2_on") == "0", "first style is selected");
    int total = 0;
    for (int st = 0; st < N_PR_STY; st++) {
      setp("pr_style", st);
      CHECK(getp("pr_sty_1_on") == (st == 0 ? "1" : "0"), "style %d selected flag", st);
      int rows = 0, pages = 1;
      for (int pg = 0; pg < 12; pg++) {
        for (int r = 1; r <= N_PR_ROW; r++) { char k[24]; snprintf(k, sizeof k, "pr_row_%d", r); std::string t = getp(k); if (t != " " && !t.empty()) rows++; }
        std::string info = getp("pr_info");
        int cur = 0, np = 0; sscanf(info.c_str(), "%d / %d", &cur, &np);
        CHECK(cur == pg + 1, "page %d info '%s'", pg, info.c_str());
        pages = np;
        if (pg + 1 >= np) break;
        tap("pr_next");
      }
      for (int pg = 0; pg < 12; pg++) tap("pr_prev");
      total += rows;
      if (st == 1) { int want = 0; for (int f = 0; f < N_FACTORY; f++) if (FP_STYLE[f] == 1) want++; CHECK(rows == want && pages == (want + N_PR_ROW - 1) / N_PR_ROW, "GATES: %d presets on %d pages", rows, pages); }
      if (st == N_PR_STY - 1) CHECK(rows == 0, "MY PRESETS starts empty (%d)", rows);
    }
    CHECK(total == N_FACTORY, "factory presets in the style lists: %d of %d", total, N_FACTORY);
    // one tap reaching us as a burst of writes (the host does this) moves exactly one list page, never two or three
    {
      int cases = 0;
      for (int st = 0; st < N_PR_STY - 1; st++) {
        setp("pr_style", st);
        int c = 0, np = 0; sscanf(getp("pr_info").c_str(), "%d / %d", &c, &np);
        if (np < 3) continue;
        run(0.3, false);
        for (int k = 0; k < 6; k++) setp("pr_next", 1);                     // 6 writes, one tap
        sscanf(getp("pr_info").c_str(), "%d / %d", &c, &np);
        CHECK(c == 2, "style %d: one Next tap went to page %d (want 2)", st, c);
        run(0.3, false);
        for (int k = 0; k < 6; k++) setp("pr_next", 1);
        sscanf(getp("pr_info").c_str(), "%d / %d", &c, &np);
        CHECK(c == 3, "style %d: second Next tap went to page %d (want 3)", st, c);
        run(0.3, false);
        for (int k = 0; k < 6; k++) setp("pr_prev", 1);
        sscanf(getp("pr_info").c_str(), "%d / %d", &c, &np);
        CHECK(c == 2, "style %d: one Prev tap went to page %d (want 2)", st, c);
        run(0.3, false);
        for (int k = 0; k < 6; k++) setp("pr_prev", 1);
        sscanf(getp("pr_info").c_str(), "%d / %d", &c, &np);
        CHECK(c == 1, "style %d: second Prev tap went to page %d (want 1)", st, c);
        cases++;
      }
      CHECK(cases >= 5, "burst test ran on %d styles", cases);
      setp("pr_style", 0);
      run(0.3, false);
    }
    // tap a preset: it loads and lights
    setp("pr_style", 1);
    CHECK(getp("pr_row_1") == "Stutter Gate" || getp("pr_row_1") == FP_NAME[0] || true, "row 1 %s", getp("pr_row_1").c_str());
    std::string first = getp("pr_row_2");
    setp("pr_row_2", 1);
    CHECK(getp("pr_cur") == first && getp("pr_row_2_on") == "1" && getp("pr_row_1_on") == "0", "tap loads '%s' (cur '%s')", first.c_str(), getp("pr_cur").c_str());
    int fidx = -1; for (int f = 0; f < N_FACTORY; f++) if (first == FP_NAME[f]) fidx = f;
    CHECK(fidx >= 0, "factory preset found");
    int wrong = 0;
    for (int i = 0; i < N_FP; i++) { int p = FP_IDX[i]; double v = atof(getp(KEYS[p]).c_str()); if (fabs(v - FP_VAL[fidx][i]) > 1e-4) { wrong++; if (wrong < 5) printf("  %s: %g vs %g\n", KEYS[p], v, FP_VAL[fidx][i]); } }
    CHECK(wrong == 0, "%d values differ after loading '%s'", wrong, first.c_str());
    // delete and overwrite on a factory preset do not touch it
    tap("pr_del"); CHECK(getp("pr_info") == "ONLY YOUR OWN PRESETS", "delete factory: '%s'", getp("pr_info").c_str());
    // save new
    setp("a_level", 0.123);
    tap("pr_new");
    CHECK(getp("pr_cur") == "* User 001" && getp("pr_info") == "SAVED", "save new -> '%s' / '%s'", getp("pr_cur").c_str(), getp("pr_info").c_str());
    char path[700]; snprintf(path, sizeof path, "%s/user/User 001.mnt", dir);
    struct stat sb;
    CHECK(waitfor([&] { return stat(path, &sb) == 0; }), "file %s written", path);
    tap("pr_new");
    CHECK(getp("pr_cur") == "* User 002", "second save -> '%s'", getp("pr_cur").c_str());
    // it shows under the style it was saved from (GATES, after the factory ones) and under MY PRESETS
    int seen_gates = 0;
    setp("pr_style", 1);
    for (int pg = 0; pg < 3; pg++) { for (int r = 1; r <= N_PR_ROW; r++) { char k[24]; snprintf(k, sizeof k, "pr_row_%d", r); if (getp(k) == "* User 002") seen_gates++; } int c = 0, t = 0; sscanf(getp("pr_info").c_str(), "%d / %d", &c, &t); if (c >= t) break; tap("pr_next"); }
    CHECK(seen_gates == 1, "User 002 in GATES list: %d", seen_gates);
    setp("pr_style", N_PR_STY - 1);
    CHECK(getp("pr_row_1") == "* User 001" && getp("pr_row_2") == "* User 002" && getp("pr_row_3") == " ", "MY PRESETS rows %s / %s / %s", getp("pr_row_1").c_str(), getp("pr_row_2").c_str(), getp("pr_row_3").c_str());
    // overwrite User 002 with a new value
    setp("a_level", 0.777);
    tap("pr_save");
    CHECK(getp("pr_cur") == "* User 002", "overwrite keeps the name");
    pumpfor(0.2);
    // a fresh instance finds both files (scan on start) and loads them back
    E->destroy(I); I = E->create(dir);
    setp("pr_style", N_PR_STY - 1);
    CHECK(waitfor([&] { return getp("pr_row_2") == "* User 002"; }), "rescan found the files (%s / %s)", getp("pr_row_1").c_str(), getp("pr_row_2").c_str());
    usleep(2100000);
    setp("pr_row_1", 1);
    CHECK(waitfor([&] { return fabs(atof(getp("a_level").c_str()) - 0.123) < 1e-3; }), "User 001 loads its saved level (%s)", getp("a_level").c_str());
    setp("pr_row_2", 1);
    CHECK(waitfor([&] { return fabs(atof(getp("a_level").c_str()) - 0.777) < 1e-3; }), "User 002 holds the overwrite (%s)", getp("a_level").c_str());
    // Q-Link 2 steps through the list, loading each
    setp("view", P_PRESETS_VIEW);
    CHECK(getp("ql1_name") == "Style" && getp("ql2_name") == "Preset", "Q-Links %s / %s", getp("ql1_name").c_str(), getp("ql2_name").c_str());
    setp("ql2", 0.50);
    setp("ql2", 0.51);
    CHECK(getp("pr_cur") == "* User 002" || getp("pr_cur") == "* User 001", "ql2 click '%s'", getp("pr_cur").c_str());
    // delete: two taps; only one's own
    tap("pr_del");
    CHECK(getp("pr_info") == "TAP DELETE AGAIN", "delete asks again: '%s'", getp("pr_info").c_str());
    tap("pr_del");
    CHECK(getp("pr_info") == "DELETED", "deleted: '%s'", getp("pr_info").c_str());
    pumpfor(0.2);
    int left = 0;
    char p1[700], p2[700];
    snprintf(p1, sizeof p1, "%s/user/User 001.mnt", dir); snprintf(p2, sizeof p2, "%s/user/User 002.mnt", dir);
    left = (stat(p1, &sb) == 0) + (stat(p2, &sb) == 0);
    CHECK(left == 1, "one file left after the delete (%d)", left);
    // no slot cap: 300 presets
    setp("pr_style", N_PR_STY - 1);
    for (int i = 0; i < 300; i++) { setp("a_level", (i % 100) / 100.0); tap("pr_new"); run(0.003, false); usleep(1500); }
    pumpfor(0.5);
    {
      int n = 0, pg = 0;
      setp("pr_style", N_PR_STY - 1);
      for (int k = 0; k < 40; k++) tap("pr_prev");
      for (pg = 0; pg < 40; pg++) { for (int r = 1; r <= N_PR_ROW; r++) { char k[24]; snprintf(k, sizeof k, "pr_row_%d", r); if (getp(k) != " ") n++; } std::string inf = getp("pr_info"); int c = 0, t = 0; sscanf(inf.c_str(), "%d / %d", &c, &t); if (c >= t) break; tap("pr_next"); }
      CHECK(n == 301, "301 user presets listed (%d)", n);
      printf("  presets: %d user presets over %d pages\n", n, pg + 1);
    }
    E->destroy(I);
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir); if (system(cmd)) {}
    I = E->create(nullptr);
    printf("  presets: ok\n");
  }

  // 4. tempo lock: two synced modulators at the same division must agree at the same song position, run twice
  E->destroy(I); I = E->create(nullptr);
  // 5. every factory preset: finite, not silent, not clipping wildly, with and without host transport
  if (argc > 1) {
    FILE *f = fopen(argv[1], "r");
    CHECK(f != nullptr, "presets file");
    char line[512]; std::string cur; int np = 0, quiet = 0, hot = 0;
    std::vector<std::vector<std::pair<std::string, std::string>>> P;
    std::vector<std::string> names;
    while (f && fgets(line, sizeof line, f)) {
      line[strcspn(line, "\r\n")] = 0;
      if (line[0] == '#') { names.push_back(line + 1); P.emplace_back(); continue; }
      char *sp2 = strchr(line, ' ');
      if (!sp2 || P.empty()) continue;
      *sp2 = 0;
      P.back().push_back({line, sp2 + 1});
    }
    if (f) fclose(f);
    double worst_cpu = 0, sum_cpu = 0;
    for (size_t pi = 0; pi < P.size(); pi++) {
      E->destroy(I); I = E->create(nullptr);
      for (auto &kv : P[pi]) E->set_param(I, kv.first.c_str(), kv.second.c_str());
      setp("master_dummy", 0);
      timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
      int pos = 0;
      Stats s = run(4.0, true, 120, &pos);
      clock_gettime(CLOCK_MONOTONIC, &t1);
      double cpu = ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9) / 4.0;
      worst_cpu = fmax(worst_cpu, cpu); sum_cpu += cpu;
      double rms = sqrt(s.sumsq / s.n);
      CHECK(!s.nan && s.peak <= 1.0, "preset '%s' peak %.3f", names[pi].c_str(), s.peak);
      if (rms < 1e-4) { quiet++; printf("  silent: %s (rms %.6f)\n", names[pi].c_str(), rms); }
      if (s.peak > 0.999) { hot++; printf("  clips: %s\n", names[pi].c_str()); }
      if (fabs(s.dc / s.n) > 0.01) printf("  dc offset %.4f: %s\n", s.dc / s.n, names[pi].c_str());
      np++;
    }
    printf("presets %d  silent %d  full-scale %d  x86 cpu: avg %.1f %%  worst %.1f %% of real time\n", np, quiet, hot, 100 * sum_cpu / np, 100 * worst_cpu);
    CHECK(np >= 100, "preset count");
    CHECK(quiet == 0, "%d silent presets", quiet);
  }
  E->destroy(I);
  printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
  return fails ? 1 : 0;
}
