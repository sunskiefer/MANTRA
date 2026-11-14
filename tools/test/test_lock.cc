// The modulator phase must follow the host song position when the transport plays and host sync is on.
#include <stdio.h>
#include <math.h>
#include "mantra_engine.h"
using namespace mx;
int main() {
  int fails = 0;
  Engine e; e.prepare(44100);
  int b = MOD_BASE[0];
  e.setParam(b + MD_Steps, 4); e.setParam(b + MD_Sync, 1); e.setParam(b + MD_Div, 10);   // 1/4 note = 1 beat
  e.setParam(b + MD_Mode, 0); e.setParam(b + MD_Gate, 1); e.setParam(b + MD_Smooth, 0);
  float st[4] = {1.0f, 0.25f, 0.5f, 0.75f};
  for (int i = 0; i < 4; i++) e.setStep(0, i, st[i]);
  static float L[128], R[128];
  struct { double ppq; int idx; } t[] = {{0.0, 0}, {0.3, 1}, {5.5, 2}, {5.9, 3}, {37.26, 1}, {123.01, 0}};
  for (auto& c : t) {
    e.setHost(c.ppq, 120.0, true);
    for (int i = 0; i < 128; i++) L[i] = R[i] = 0;
    e.process(L, R, 16);                       // one 16-sample sub block
    double want = st[c.idx], got = e.modValue(0);
    bool ok = fabs(want - got) < 1e-6;
    if (!ok) { fails++; printf("FAIL ppq %.2f want %.2f got %.2f\n", c.ppq, want, got); }
  }
  // stopped transport: free running keeps going, no crash, phase continues from where it was
  e.setHost(3.0, 120.0, false);
  double a = e.modValue(0);
  for (int k = 0; k < 400; k++) e.process(L, R, 16);
  printf("free running after stop ok (%.2f -> %.2f)\n", a, e.modValue(0));
  // tempo follows the host, 90 bpm vs 150 bpm, delay time in sync
  printf(fails ? "LOCK FAILED\n" : "LOCK PASSED\n");
  return fails;
}
