#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define REPEAT    5000
#define MAX_DEPTH 10

// ── helper: tạo tên "perf_lX" ───────────────────────────
void make_name(char *buf, int num)
{
  strcpy(buf, "perf_l");

  if(num >= 10){
    buf[6] = '0' + num / 10;
    buf[7] = '0' + num % 10;
    buf[8] = '\0';
  } else {
    buf[6] = '0' + num;
    buf[7] = '\0';
  }
}

// ── tạo file gốc ───────────────────────────────────────
void make_base(const char *path)
{
  int fd = open(path, O_WRONLY | O_CREATE);
  if(fd < 0){
    fprintf(2, "perftest: cannot create base\n");
    exit(1);
  }
  write(fd, "hello", 5);
  close(fd);
}

// ── tạo chain symlink ──────────────────────────────────
void make_chain(int depth)
{
  char prev[16], curr[16];

  // perf_l1 → perf_base
  make_name(curr, 1);
  if(symlink("perf_base", curr) < 0){
    fprintf(2, "perftest: symlink failed at depth 1\n");
    exit(1);
  }

  for(int i = 2; i <= depth; i++){
    make_name(prev, i-1);
    make_name(curr, i);

    if(symlink(prev, curr) < 0){
      fprintf(2, "perftest: symlink failed at depth %d\n", i);
      exit(1);
    }
  }
}

// ── benchmark ──────────────────────────────────────────
int bench(const char *path)
{
  int check = open(path, O_RDONLY);
  if(check < 0){
    fprintf(2, "perftest: cannot open %s\n", path);
    return -1;
  }
  close(check);

  int start = uptime();

  for(int i = 0; i < REPEAT; i++){
    int fd = open(path, O_RDONLY);
    if(fd >= 0){
      char buf[1];
      read(fd, buf, 1);
      close(fd);
    }
  }

  return uptime() - start;
}

// ── cleanup ────────────────────────────────────────────
void cleanup()
{
  char name[16];
  unlink("perf_base");

  for(int i = 1; i <= MAX_DEPTH; i++){
    make_name(name, i);
    unlink(name);
  }
}

// ── main ───────────────────────────────────────────────
int main(void)
{
  printf("=== Symlink Performance Test ===\n");
  printf("Repeat: %d\n\n", REPEAT);

  make_base("perf_base");
  make_chain(MAX_DEPTH);

  int results[MAX_DEPTH + 1];

  // direct
  results[0] = bench("perf_base");
  printf("depth 0 (direct) : %d ticks\n", results[0]);

  // từng tầng
  char path[16];
  for(int d = 1; d <= MAX_DEPTH; d++){
    make_name(path, d);

    int t = bench(path);
    results[d] = (t >= 0) ? t : 0;

    if(t < 0){
      printf("depth %d : FAILED\n", d);
      continue;
    }

    int pct = (results[0] > 0) ? (t - results[0]) * 100 / results[0] : 0;
    printf("depth %d : %d ticks (%d%%)\n", d, t, pct);
  }

  // ── analysis ────────────────────────────────────────
  printf("\n--- Analysis ---\n");

  if(results[0] > 0 && results[1] > 0){
    int d1  = results[1]  - results[0];
    int d5  = results[5]  - results[0];
    int d10 = results[10] - results[0];

    printf("Overhead depth 1  : %d ticks\n", d1);
    printf("Overhead depth 5  : %d ticks\n", d5);
    printf("Overhead depth 10 : %d ticks\n", d10);

    int delta_lo = results[5]  - results[1];
    int delta_hi = results[10] - results[5];

    printf("Delta 1→5  : %d ticks\n", delta_lo);
    printf("Delta 5→10 : %d ticks\n", delta_hi);

    if(delta_lo > 0){
      int ratio = delta_hi * 100 / delta_lo;
      printf("Linearity ratio : %d%% → ", ratio);

      if(ratio > 80 && ratio < 120)
        printf("LINEAR\n");
      else if(ratio >= 120)
        printf("SUPER-LINEAR\n");
      else
        printf("SUB-LINEAR\n");
    }
  }

  cleanup();
  printf("\n=== Done ===\n");
  exit(0);
}
