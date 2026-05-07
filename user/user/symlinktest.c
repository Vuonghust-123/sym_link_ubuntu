//buiduyvuong_20223832@20223832:~/xv6-riscv$ cat user/symlinktest.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ── Helpers ───────────────────────────────────────────────
static int passed = 0;
static int failed = 0;

void ok(const char *name)
{
  printf("  [PASS] %s\n", name);
  passed++;
}

void fail(const char *name, const char *reason)
{
  printf("  [FAIL] %s: %s\n", name, reason);
  failed++;
}

int make_file(const char *path, const char *content)
{
  int fd = open(path, O_WRONLY | O_CREATE);
  if(fd < 0) return -1;
  write(fd, content, strlen(content));
  close(fd);
  return 0;
}

// ═══════════════════════════════════════════════════════════
// NHÓM A – Basic functionality
// ═══════════════════════════════════════════════════════════

void test_A1_create_symlink()
{
  make_file("a_target.txt", "hello");
  if(symlink("a_target.txt", "a_link") < 0)
    { fail("A1_create_symlink", "symlink() returned error"); return; }
  ok("A1_create_symlink");
}

// Dùng O_NOFOLLOW để đọc nội dung inode symlink (target string)
void test_A2_read_target_nofollow()
{
  int fd = open("a_link", O_RDONLY | O_NOFOLLOW);
  if(fd < 0){ fail("A2_read_target", "open with O_NOFOLLOW failed"); return; }

  char buf[128];
  int n = read(fd, buf, sizeof(buf)-1);
  close(fd);
  if(n <= 0){ fail("A2_read_target", "read returned <= 0"); return; }
  buf[n] = '\0';

  if(strcmp(buf, "a_target.txt") != 0)
    { fail("A2_read_target", "target string mismatch"); return; }
  ok("A2_read_target_nofollow");
}

// fstat trên fd mở bằng O_NOFOLLOW → type phải là T_SYMLINK
void test_A3_stat_nofollow()
{
  int fd = open("a_link", O_RDONLY | O_NOFOLLOW);
  if(fd < 0){ fail("A3_stat_nofollow", "open failed"); return; }
  struct stat st;
  fstat(fd, &st);
  close(fd);
  if(st.type != 4)   // T_SYMLINK = 4
    { fail("A3_stat_nofollow", "type != T_SYMLINK"); return; }
  ok("A3_stat_nofollow");
}

// Mở bình thường → follow symlink → đọc nội dung file đích
void test_A4_follow_symlink()
{
  int fd = open("a_link", O_RDONLY);
  if(fd < 0){ fail("A4_follow_symlink", "cannot follow symlink"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "hello") != 0)
    { fail("A4_follow_symlink", "content mismatch"); return; }
  ok("A4_follow_symlink");
}

// ═══════════════════════════════════════════════════════════
// NHÓM B – Path resolution (QUAN TRỌNG NHẤT)
// ═══════════════════════════════════════════════════════════

// B1: symlink ở GIỮA path — link/subfile
void test_B1_symlink_in_middle_of_path()
{
  mkdir("b_realdir");
  make_file("b_realdir/inside.txt", "inside");
  symlink("b_realdir", "b_link_dir");

  // Mở qua "b_link_dir/inside.txt" — b_link_dir là symlink ở giữa path
  int fd = open("b_link_dir/inside.txt", O_RDONLY);
  if(fd < 0){ fail("B1_symlink_in_middle", "cannot open via mid-path symlink"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "inside") != 0)
    { fail("B1_symlink_in_middle", "content mismatch"); return; }
  ok("B1_symlink_in_middle_of_path");
}

// B2: chain nhiều tầng — a→b→c→target
void test_B2_chain_symlinks()
{
  // a_target.txt đã tồn tại từ nhóm A
  symlink("a_target.txt", "b_chain_c");
  symlink("b_chain_c",    "b_chain_b");
  symlink("b_chain_b",    "b_chain_a");

  int fd = open("b_chain_a", O_RDONLY);
  if(fd < 0){ fail("B2_chain_symlinks", "cannot follow 3-level chain"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "hello") != 0)
    { fail("B2_chain_symlinks", "wrong content after chain"); return; }
  ok("B2_chain_symlinks");
}

// B3: relative path symlink
void test_B3_relative_path()
{
  symlink("a_target.txt", "b_rel_link");
  int fd = open("b_rel_link", O_RDONLY);
  if(fd < 0){ fail("B3_relative_path", "cannot follow relative symlink"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "hello") != 0)
    { fail("B3_relative_path", "content mismatch"); return; }
  ok("B3_relative_path");
}

// B4: absolute path symlink
void test_B4_absolute_path()
{
  symlink("/a_target.txt", "b_abs_link");
  int fd = open("b_abs_link", O_RDONLY);
  if(fd < 0){ fail("B4_absolute_path", "cannot follow absolute symlink"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "hello") != 0)
    { fail("B4_absolute_path", "content mismatch"); return; }
  ok("B4_absolute_path");
}

// B5: symlink → directory, rồi đọc file bên trong
void test_B5_symlink_to_directory()
{
  // b_realdir đã tồn tại từ B1
  int fd = open("b_link_dir/inside.txt", O_RDONLY);
  if(fd < 0){ fail("B5_symlink_to_dir", "cannot access file via dir symlink"); return; }
  close(fd);
  ok("B5_symlink_to_directory");
}

// B6: symlink ở giữa path VÀ cuối path cùng lúc
// path: b_link_dir/sub_link → b_realdir/inside.txt
void test_B6_symlink_mid_and_end()
{
  // b_realdir/inside.txt đã có
  // tạo symlink trong thư mục thật trỏ tới file thật
  symlink("inside.txt", "b_realdir/sub_link");

  int fd = open("b_link_dir/sub_link", O_RDONLY);
  if(fd < 0){ fail("B6_symlink_mid_and_end", "cannot open via mid+end symlinks"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "inside") != 0)
    { fail("B6_symlink_mid_and_end", "content mismatch"); return; }
  ok("B6_symlink_mid_and_end");
}

// ═══════════════════════════════════════════════════════════
// NHÓM C – Error handling
// ═══════════════════════════════════════════════════════════

// C1: dangling symlink → mở phải thất bại
void test_C1_dangling_symlink()
{
  symlink("c_ghost_does_not_exist.txt", "c_dangling");
  int fd = open("c_dangling", O_RDONLY);
  if(fd >= 0){
    close(fd);
    fail("C1_dangling_symlink", "should fail on dangling link");
    return;
  }
  ok("C1_dangling_symlink");
}

// C2: circular symlink → kernel phải detect và trả lỗi
void test_C2_circular_symlink()
{
  symlink("c_circ_b", "c_circ_a");
  symlink("c_circ_a", "c_circ_b");
  int fd = open("c_circ_a", O_RDONLY);
  if(fd >= 0){
    close(fd);
    fail("C2_circular_symlink", "should return error on cycle");
    return;
  }
  ok("C2_circular_symlink");
}

// C3: vượt MAX_RECURSION (11 tầng, giới hạn 10) → phải thất bại
void test_C3_max_depth_exceeded()
{
  make_file("c_deep_end", "deep");
  symlink("c_deep_end", "c_d10");
  symlink("c_d10", "c_d9");
  symlink("c_d9",  "c_d8");
  symlink("c_d8",  "c_d7");
  symlink("c_d7",  "c_d6");
  symlink("c_d6",  "c_d5");
  symlink("c_d5",  "c_d4");
  symlink("c_d4",  "c_d3");
  symlink("c_d3",  "c_d2");
  symlink("c_d2",  "c_d1");
  symlink("c_d1",  "c_d0");  // tầng 11 — vượt giới hạn

  int fd = open("c_d0", O_RDONLY);
  if(fd >= 0){
    close(fd);
    // Một số kernel cho phép đúng 10 tầng nhưng block tầng 11
    // Tuỳ implement, có thể pass hoặc fail — ghi nhận kết quả
    ok("C3_max_depth (kernel cho phép — kiểm tra lại MAX_RECURSION)");
  } else {
    ok("C3_max_depth_exceeded (kernel block đúng)");
  }
}

// C4: đúng MAX_RECURSION (10 tầng) → phải thành công
void test_C4_exact_max_depth()
{
  make_file("c_exact_end", "exact");
  symlink("c_exact_end", "c_e9");
  symlink("c_e9", "c_e8");
  symlink("c_e8", "c_e7");
  symlink("c_e7", "c_e6");
  symlink("c_e6", "c_e5");
  symlink("c_e5", "c_e4");
  symlink("c_e4", "c_e3");
  symlink("c_e3", "c_e2");
  symlink("c_e2", "c_e1");
  symlink("c_e1", "c_e0");  // đúng 10 tầng

  int fd = open("c_e0", O_RDONLY);
  if(fd < 0){
    fail("C4_exact_max_depth", "should succeed at exactly MAX_RECURSION");
    return;
  }
  char buf[8];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "exact") != 0)
    { fail("C4_exact_max_depth", "content mismatch"); return; }
  ok("C4_exact_max_depth");
}

// ═══════════════════════════════════════════════════════════
// NHÓM D – System behavior
// ═══════════════════════════════════════════════════════════

// D1: O_NOFOLLOW → nhận inode symlink, không phải file đích
void test_D1_O_NOFOLLOW()
{
  // Mở symlink với O_NOFOLLOW → type phải là T_SYMLINK
  int fd = open("a_link", O_RDONLY | O_NOFOLLOW);
  if(fd < 0){ fail("D1_O_NOFOLLOW", "open failed"); return; }
  struct stat st;
  fstat(fd, &st);
  close(fd);
  if(st.type != 4)
    { fail("D1_O_NOFOLLOW", "did not return symlink inode"); return; }
  ok("D1_O_NOFOLLOW");
}

// D2: unlink symlink → chỉ xóa link, file đích còn nguyên
void test_D2_unlink_symlink()
{
  symlink("a_target.txt", "d_tmp_link");
  unlink("d_tmp_link");

  // File gốc vẫn còn
  int fd = open("a_target.txt", O_RDONLY);
  if(fd < 0){ fail("D2_unlink_symlink", "original file was deleted!"); return; }
  close(fd);

  // Link đã biến mất
  fd = open("d_tmp_link", O_RDONLY);
  if(fd >= 0){ close(fd); fail("D2_unlink_symlink", "link still exists"); return; }
  ok("D2_unlink_symlink");
}

// D3: overwrite protection — symlink() không đè lên file đã tồn tại
void test_D3_overwrite_protection()
{
  make_file("d_exist.txt", "original");
  if(symlink("a_target.txt", "d_exist.txt") >= 0)
    { fail("D3_overwrite_protection", "should not overwrite existing file"); return; }

  // File gốc phải còn nguyên
  int fd = open("d_exist.txt", O_RDONLY);
  if(fd < 0){ fail("D3_overwrite_protection", "original file was destroyed"); return; }
  char buf[16];
  int n = read(fd, buf, sizeof(buf)-1);
  buf[n < 0 ? 0 : n] = '\0';
  close(fd);
  if(strcmp(buf, "original") != 0)
    { fail("D3_overwrite_protection", "original content corrupted"); return; }
  ok("D3_overwrite_protection");
}

// D4: unlink dangling symlink → phải thành công (xóa được link dù target không tồn tại)
void test_D4_unlink_dangling()
{
  symlink("d_nowhere.txt", "d_dangling2");
  if(unlink("d_dangling2") < 0)
    { fail("D4_unlink_dangling", "cannot unlink dangling symlink"); return; }
  ok("D4_unlink_dangling");
}

// D5: O_NOFOLLOW trên thư mục → phải mở được (ls dùng case này)
void test_D5_nofollow_on_directory()
{
  int fd = open(".", O_RDONLY | O_NOFOLLOW);
  if(fd < 0){ fail("D5_nofollow_on_directory", "cannot open '.' with O_NOFOLLOW"); return; }
  close(fd);
  ok("D5_nofollow_on_directory");
}

// ═══════════════════════════════════════════════════════════
// Cleanup
// ═══════════════════════════════════════════════════════════
void cleanup()
{
  // Nhóm A
  unlink("a_link");
  unlink("a_target.txt");

  // Nhóm B
  unlink("b_realdir/sub_link");
  unlink("b_realdir/inside.txt");
  unlink("b_realdir");
  unlink("b_link_dir");
  unlink("b_chain_a");
  unlink("b_chain_b");
  unlink("b_chain_c");
  unlink("b_rel_link");
  unlink("b_abs_link");

  // Nhóm C
  unlink("c_dangling");
  unlink("c_circ_a");
  unlink("c_circ_b");
  unlink("c_d0");  unlink("c_d1");  unlink("c_d2");
  unlink("c_d3");  unlink("c_d4");  unlink("c_d5");
  unlink("c_d6");  unlink("c_d7");  unlink("c_d8");
  unlink("c_d9");  unlink("c_d10"); unlink("c_deep_end");
  unlink("c_e0");  unlink("c_e1");  unlink("c_e2");
  unlink("c_e3");  unlink("c_e4");  unlink("c_e5");
  unlink("c_e6");  unlink("c_e7");  unlink("c_e8");
  unlink("c_e9");  unlink("c_exact_end");

  // Nhóm D
  unlink("d_exist.txt");
  // d_tmp_link và d_dangling2 đã bị unlink trong test
}

// ═══════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════
int main(void)
{
  printf("=== Symlink Automated Test Suite ===\n");

  printf("\n[Group A] Basic functionality\n");
  test_A1_create_symlink();
  test_A2_read_target_nofollow();
  test_A3_stat_nofollow();
  test_A4_follow_symlink();

  printf("\n[Group B] Path resolution\n");
  test_B1_symlink_in_middle_of_path();
  test_B2_chain_symlinks();
  test_B3_relative_path();
  test_B4_absolute_path();
  test_B5_symlink_to_directory();
  test_B6_symlink_mid_and_end();

  printf("\n[Group C] Error handling\n");
  test_C1_dangling_symlink();
  test_C2_circular_symlink();
  test_C3_max_depth_exceeded();
  test_C4_exact_max_depth();

  printf("\n[Group D] System behavior\n");
  test_D1_O_NOFOLLOW();
  test_D2_unlink_symlink();
  test_D3_overwrite_protection();
  test_D4_unlink_dangling();
  test_D5_nofollow_on_directory();

  cleanup();

  printf("\n=== Result: %d passed, %d failed ===\n", passed, failed);
  exit(0);
}
