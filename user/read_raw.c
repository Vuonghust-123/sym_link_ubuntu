#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[]) {
  char buf[64];
  
  int fd = open("link_to_readme", O_RDONLY | O_NOFOLLOW);
  
  if(fd < 0){
    printf("read_raw: không mở được link (có thể bạn chưa chạy symtest hoặc O_NOFOLLOW chưa chuẩn)\n");
    exit(1);
  }

  int n = read(fd, buf, sizeof(buf) - 1);
  if(n >= 0){
    buf[n] = '\0';
    printf("Nội dung bên trong link: %s\n", buf);
  } else {
    printf("read_raw: đọc thất bại\n");
  }

  close(fd);
  exit(0);
}
