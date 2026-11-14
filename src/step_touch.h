// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// MANTRA -- touch-to-value for the 32 step faders of a modulator's STEPS page.
//
// The host's own sliders move relative to where the finger lands. This reads the touchscreen itself (evdev, read-only, no grab)
// and finds where MPC's page is on the display (the markers painted into the metal background), so a touch on a step fader
// can jump to the touched height and a finger drawn across the faders sets each one it crosses.
// The method (DRM scanout read for the marker search, the multitouch slot reader) is CSXXL's XYZ pad (src/xyz_pad.c), without its
// overlay plane. One thread per process serves every MANTRA instance; the instance the user last touched owns the screen.
//
// Nothing here runs in set_param / get_param / render: the thread posts events into a lock-free ring the engine drains in
// process_fn. UNVERIFIED ON DEVICE until tested on a Force (see TESTING.md).
#pragma once
#include <atomic>
#include <stdint.h>

namespace stp {

struct Slot {
  std::atomic<int> page_mod{0};       // 0: not on a steps page, 1..8: the modulator whose steps page is showing
  std::atomic<uint32_t> last_ui{0};   // ms (monotonic) of the last UI-originated change in this instance
  std::atomic<int> touching{0};       // a finger is drawing on this instance's faders
  std::atomic<uint32_t> up_ms{0};     // when the finger last lifted (host writes are ignored for a moment after)
  std::atomic<unsigned> head{0}, tail{0};
  uint32_t ring[512];

  bool push(uint32_t e) {   // touch thread
    unsigned h = head.load(std::memory_order_relaxed), n = (h + 1) & 511;
    if (n == tail.load(std::memory_order_acquire)) return false;
    ring[h] = e;
    head.store(n, std::memory_order_release);
    return true;
  }
  bool pop(uint32_t *e) {   // engine
    unsigned t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    *e = ring[t];
    tail.store((t + 1) & 511, std::memory_order_release);
    return true;
  }
};
// event: bits 0..15 value (0..65535), 16..20 step (0..31), 21..23 modulator (0..7)
inline uint32_t pack(int mod, int step, int val) { return (uint32_t)val | ((uint32_t)step << 16) | ((uint32_t)mod << 21); }
inline int ev_val(uint32_t e) { return (int)(e & 0xffff); }
inline int ev_step(uint32_t e) { return (int)((e >> 16) & 31); }
inline int ev_mod(uint32_t e) { return (int)((e >> 21) & 7); }

uint32_t now_ms();
// register / unregister an instance; data_dir (may be null) gets touch.log; a file touch.off in it disables the reader
void attach(Slot *s, const char *data_dir);
void detach(Slot *s);

}  // namespace stp
