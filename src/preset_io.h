// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// MANTRA -- user preset files and the worker thread that reads / writes them.
//
// The audio / host side never touches the disk (framework rule: no I/O in set_param / get_param / render). It posts a job here
// and picks the answer up later with poll() (a trylock, never a wait):
//   SCAN    list <dir>/user/*.mnt (name from the file name, style from the first lines)
//   SAVE    write <dir>/user/<name>.mnt from a copy of the values the engine took when the job was posted
//   DELETE  remove <dir>/user/<name>.mnt
//   LOAD    read <dir>/user/<name>.mnt into a value array (NaN = not in the file)
// One file per preset, any number of them: there is no slot cap. File format (text, so a PC can read / edit / copy it):
//   MANTRA-PRESET 1
//   name User 001
//   style GATES
//   <parameter key> <value>        one line per parameter (keys are the plugin's own, so a later version keeps loading it)
#pragma once
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace mxf {

struct UEnt { char name[32]; uint8_t style; };

enum { IO_SCAN = 1, IO_SAVE, IO_DELETE, IO_LOAD };

struct IoRes {
  int type = 0, ok = 0, n = 0;
  char name[32] = {0};
  float *vals = nullptr;     // LOAD: N floats (NaN = missing); the receiver frees it
  UEnt *list = nullptr;      // SCAN: n entries; the receiver frees it
};

class PresetIO {
 public:
  typedef int (*FindFn)(const char *key);
  static constexpr int QMAX = 64;

  void start(const char *dir, const char *const *keys, const int16_t *idx, int n, FindFn find, const char *const *styles, int nstyles) {
    snprintf(dir_, sizeof dir_, "%s", dir ? dir : "");
    keys_ = keys; idx_ = idx; n_ = n; find_ = find; styles_ = styles; nstyles_ = nstyles;
    pthread_mutex_init(&jm_, nullptr);
    pthread_cond_init(&jc_, nullptr);
    pthread_mutex_init(&rm_, nullptr);
    if (!dir_[0]) return;
    run_ = true;
    if (pthread_create(&th_, nullptr, &PresetIO::entry, this) == 0) started_ = true;
  }
  void stop() {
    if (started_) {
      pthread_mutex_lock(&jm_);
      run_ = false;
      pthread_cond_signal(&jc_);
      pthread_mutex_unlock(&jm_);
      pthread_join(th_, nullptr);
      started_ = false;
    }
    for (int i = 0; i < jn_; i++) free(jobs_[i].vals);
    jn_ = 0;
    for (int i = 0; i < rn_; i++) { free(res_[i].vals); free(res_[i].list); }
    rn_ = 0;
  }
  bool enabled() const { return started_; }

  // queue a job (vals: n floats, copied). Returns false when the queue is full or there is no data folder.
  bool post(int type, const char *name, int style, const float *vals) {
    if (!started_) return false;
    pthread_mutex_lock(&jm_);
    if (jn_ >= QMAX) { pthread_mutex_unlock(&jm_); return false; }
    Job &j = jobs_[jn_++];
    j.type = type;
    snprintf(j.name, sizeof j.name, "%s", name ? name : "");
    j.style = (uint8_t)style;
    j.vals = nullptr;
    if (vals && type == IO_SAVE) { j.vals = (float *)malloc(sizeof(float) * n_); memcpy(j.vals, vals, sizeof(float) * n_); }
    pthread_cond_signal(&jc_);
    pthread_mutex_unlock(&jm_);
    return true;
  }
  // an answer, if one is ready and the mailbox is free right now
  bool poll(IoRes &out) {
    if (!started_ || pthread_mutex_trylock(&rm_) != 0) return false;
    bool got = false;
    if (rn_ > 0) {
      out = res_[0];
      for (int i = 1; i < rn_; i++) res_[i - 1] = res_[i];
      rn_--;
      got = true;
    }
    pthread_mutex_unlock(&rm_);
    return got;
  }

  static void file_name(const char *dir, const char *name, char *buf, size_t n) { snprintf(buf, n, "%s/user/%s.mnt", dir, name); }

 private:
  struct Job { int type; char name[32]; uint8_t style; float *vals; };
  char dir_[512] = {0};
  const char *const *keys_ = nullptr;
  const int16_t *idx_ = nullptr;
  int n_ = 0;
  FindFn find_ = nullptr;
  const char *const *styles_ = nullptr;
  int nstyles_ = 0;
  pthread_t th_;
  pthread_mutex_t jm_, rm_;
  pthread_cond_t jc_;
  bool run_ = false, started_ = false;
  Job jobs_[QMAX];
  int jn_ = 0;
  IoRes res_[QMAX * 2];
  int rn_ = 0;

  static void *entry(void *self) { ((PresetIO *)self)->loop(); return nullptr; }

  void push(const IoRes &r) {
    pthread_mutex_lock(&rm_);
    if (rn_ < QMAX * 2) res_[rn_++] = r;
    else { free(r.vals); free(r.list); }
    pthread_mutex_unlock(&rm_);
  }

  void loop() {
    if (nice(10)) {}
    for (;;) {
      pthread_mutex_lock(&jm_);
      while (jn_ == 0 && run_) pthread_cond_wait(&jc_, &jm_);
      if (jn_ == 0 && !run_) { pthread_mutex_unlock(&jm_); return; }
      Job j = jobs_[0];
      for (int i = 1; i < jn_; i++) jobs_[i - 1] = jobs_[i];
      jn_--;
      pthread_mutex_unlock(&jm_);
      IoRes r;
      r.type = j.type;
      snprintf(r.name, sizeof r.name, "%s", j.name);
      switch (j.type) {
        case IO_SCAN: r.ok = do_scan(r); break;
        case IO_SAVE: r.ok = do_save(j); break;
        case IO_DELETE: r.ok = do_delete(j); break;
        case IO_LOAD: r.ok = do_load(j, r); break;
      }
      free(j.vals);
      push(r);
    }
  }

  int style_index(const char *s) const {
    for (int i = 0; i < nstyles_; i++) if (!strcmp(s, styles_[i])) return i;
    return nstyles_ - 1;     // anything else: the last style (MY PRESETS)
  }

  bool user_dir(char *buf, size_t n) {
    snprintf(buf, n, "%s", dir_);
    mkdir(buf, 0777);
    snprintf(buf, n, "%s/user", dir_);
    mkdir(buf, 0777);
    struct stat st;
    return stat(buf, &st) == 0 && S_ISDIR(st.st_mode);
  }

  static bool good_name(const char *n) {
    if (!n[0]) return false;
    for (const char *c = n; *c; c++) if (*c == '/' || *c == '\\' || (*c == '.' && c == n)) return false;
    return true;
  }

  int do_scan(IoRes &r) {
    char ud[600];
    if (!user_dir(ud, sizeof ud)) return 0;
    DIR *d = opendir(ud);
    if (!d) return 0;
    int cap = 64, n = 0;
    UEnt *list = (UEnt *)malloc(sizeof(UEnt) * cap);
    while (struct dirent *e = readdir(d)) {
      size_t l = strlen(e->d_name);
      if (l < 5 || strcmp(e->d_name + l - 4, ".mnt") || e->d_name[0] == '.') continue;
      if (n == cap) { cap *= 2; list = (UEnt *)realloc(list, sizeof(UEnt) * cap); }
      UEnt &u = list[n];
      memset(&u, 0, sizeof u);
      size_t nl = l - 4 < sizeof u.name - 1 ? l - 4 : sizeof u.name - 1;
      memcpy(u.name, e->d_name, nl);
      u.style = (uint8_t)(nstyles_ - 1);
      char path[900];
      snprintf(path, sizeof path, "%s/%s", ud, e->d_name);
      if (FILE *f = fopen(path, "r")) {
        char line[160];
        for (int k = 0; k < 4 && fgets(line, sizeof line, f); k++) {
          if (!strncmp(line, "style ", 6)) {
            char *s = line + 6;
            s[strcspn(s, "\r\n")] = 0;
            u.style = (uint8_t)style_index(s);
          }
        }
        fclose(f);
      }
      n++;
    }
    closedir(d);
    r.list = list;
    r.n = n;
    return 1;
  }

  int do_save(const Job &j) {
    if (!good_name(j.name) || !j.vals) return 0;
    char ud[600], path[900], tmp[920];
    if (!user_dir(ud, sizeof ud)) return 0;
    snprintf(path, sizeof path, "%s/%s.mnt", ud, j.name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "MANTRA-PRESET 1\nname %s\nstyle %s\n", j.name, styles_[j.style < nstyles_ ? j.style : nstyles_ - 1]);
    for (int i = 0; i < n_; i++) fprintf(f, "%s %.6g\n", keys_[idx_[i]], (double)j.vals[i]);
    int ok = fflush(f) == 0;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp, path) == 0;
    else remove(tmp);
    return ok;
  }

  int do_delete(const Job &j) {
    if (!good_name(j.name)) return 0;
    char path[900];
    snprintf(path, sizeof path, "%s/user/%s.mnt", dir_, j.name);
    return remove(path) == 0;
  }

  int do_load(const Job &j, IoRes &r) {
    if (!good_name(j.name)) return 0;
    char path[900];
    snprintf(path, sizeof path, "%s/user/%s.mnt", dir_, j.name);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    float *v = (float *)malloc(sizeof(float) * n_);
    for (int i = 0; i < n_; i++) v[i] = NAN;
    // key -> position: the parameter index the key names, searched in the preset's own index list
    char line[200];
    int seen = 0;
    while (fgets(line, sizeof line, f)) {
      char *sp = strchr(line, ' ');
      if (!sp) continue;
      *sp = 0;
      int p = find_ ? find_(line) : -1;
      if (p < 0) continue;
      int lo = 0, hi = n_ - 1;       // idx_ is ascending
      while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (idx_[mid] == p) { v[mid] = (float)atof(sp + 1); seen++; break; }
        if (idx_[mid] < p) lo = mid + 1; else hi = mid - 1;
      }
    }
    fclose(f);
    if (!seen) { free(v); return 0; }
    r.vals = v;
    return 1;
  }
};

}  // namespace mxf
