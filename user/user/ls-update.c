//buiduyvuong_20223832@20223832:~/xv6-riscv$ cat user/ls.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
type_to_str(int type)
{
  if (type == 1) return "d"; // T_DIR
  if (type == 2) return "f"; // T_FILE
  if (type == 3) return "c"; // T_DEVICE
  if (type == 4) return "l"; // T_SYMLINK
  return "?";
}

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
  buf[sizeof(buf)-1] = '\0';
  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // SỬA 4: Dùng O_NOFOLLOW ngay từ đầu để không follow symlink
  if((fd = open(path, O_RDONLY | O_NOFOLLOW)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    printf("%s %s %d %d\n", fmtname(path), type_to_str(st.type), st.ino, (int)st.size);
    break;

  // SỬA 3: T_SYMLINK tách thành case riêng, đúng vị trí trong switch
  case T_SYMLINK:
    {
      char target[128];
      int n = read(fd, target, sizeof(target) - 1);
      if(n < 0) n = 0;
      target[n] = '\0';
      printf("%s %s -> %s\n", fmtname(path), type_to_str(st.type), target);
    }
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = '\0';

      // SỬA 1: Dùng open + O_NOFOLLOW thay vì stat() để detect symlink đúng
      int fd2 = open(buf, O_RDONLY | O_NOFOLLOW);
      if(fd2 < 0){
        printf("ls: cannot open %s\n", buf);
        continue;
      }
      if(fstat(fd2, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        close(fd2);
        continue;
      }

      // SỬA 2: Nếu là symlink → đọc target và hiển thị dạng "name l -> target"
      if(st.type == T_SYMLINK){
        char target[128];
        int n = read(fd2, target, sizeof(target) - 1);
        if(n < 0) n = 0;
        target[n] = '\0';
        printf("%s %s -> %s\n", fmtname(buf), type_to_str(st.type), target);
      } else {
        printf("%s %s %d %d\n", fmtname(buf), type_to_str(st.type), st.ino, (int)st.size);
      }

      // SỬA 3: Đóng fd2 sau mỗi entry
      close(fd2);
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }
  for(i = 1; i < argc; i++)
    ls(argv[i]);
  exit(0);
}
