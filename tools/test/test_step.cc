// Offline test of the step-touch event path (mailbox, packing, finger -> step / value, slide-draw interpolation).
//   g++ -std=gnu++17 -O2 -Isrc -Ibuild tools/test/test_step.cc -lpthread -o build/test_step && build/test_step
#include "../../src/step_touch.cc"
#include <assert.h>
using namespace stp;
using namespace mxf;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  Slot s;
  // pack / unpack
  uint32_t e = pack(7, 31, 65535);
  CHECK(ev_mod(e) == 7 && ev_step(e) == 31 && ev_val(e) == 65535);
  e = pack(0, 0, 0);
  CHECK(ev_mod(e) == 0 && ev_step(e) == 0 && ev_val(e) == 0);
  // ring: fills to 511, refuses the 512th, drains in order
  int n = 0;
  while (s.push(pack(1, n & 31, n))) n++;
  CHECK(n == 511);
  for (int i = 0; i < 511; i++) { uint32_t x; CHECK(s.pop(&x)); CHECK(ev_val(x) == i); }
  uint32_t x;
  CHECK(!s.pop(&x));
  // geometry: column centres and the fader's ends
  CHECK(col_of((float)ST_X0) == 0 && col_of((float)(ST_X0 + ST_DX * 31)) == 31);
  CHECK(col_of(-50) == 0 && col_of(5000) == 31);
  CHECK(col_of((float)(ST_X0 + ST_DX * 5 + ST_DX / 2 - 1)) == 5 && col_of((float)(ST_X0 + ST_DX * 5 + ST_DX / 2 + 1)) == 6);
  CHECK(val_of((float)(ST_Y0 + INSET)) == 65535);
  CHECK(val_of((float)(ST_Y0 + ST_H - INSET)) == 0);
  CHECK(val_of(-100) == 65535 && val_of(1000) == 0);
  int mid = val_of((float)(ST_Y0 + ST_H / 2));
  CHECK(mid > 32000 && mid < 33500);
  // a finger: down on step 3 near the top, then slides right and down to step 8
  Touch t;
  t.claim_slot = &s;
  t.claim_mod = 2;
  draw_to(t, (float)(ST_X0 + 3 * ST_DX), (float)(ST_Y0 + INSET), true);
  CHECK(s.pop(&x) && ev_mod(x) == 2 && ev_step(x) == 3 && ev_val(x) == 65535);
  CHECK(!s.pop(&x));
  draw_to(t, (float)(ST_X0 + 3 * ST_DX), (float)(ST_Y0 + INSET), false);   // same place: nothing
  CHECK(!s.pop(&x));
  draw_to(t, (float)(ST_X0 + 8 * ST_DX), (float)(ST_Y0 + ST_H - INSET), false);
  int prev = 65535;
  for (int c = 4; c <= 8; c++) {
    CHECK(s.pop(&x) && ev_step(x) == c);
    CHECK(ev_val(x) < prev);          // falls steadily
    prev = ev_val(x);
  }
  CHECK(prev == 0 && !s.pop(&x));
  // and back to the left, over two steps, rising
  draw_to(t, (float)(ST_X0 + 6 * ST_DX), (float)(ST_Y0 + INSET), false);
  CHECK(s.pop(&x) && ev_step(x) == 7 && ev_val(x) > 0);
  CHECK(s.pop(&x) && ev_step(x) == 6 && ev_val(x) == 65535);
  CHECK(!s.pop(&x));
  printf(fails ? "FAILED\n" : "step touch: ok\n");
  return fails ? 1 : 0;
}
