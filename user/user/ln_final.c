// user/ln.c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int sflag = 0;
  int i = 1;


  
//Đoạn mã này kiểm tra xem người dùng có nhập tham số -s ngay sau tên lệnh hay không.
//Nếu có: sflag được bật lên 1 và i được đặt là 2. Điều này có nghĩa là đường dẫn đích sẽ nằm ở argv và tên liên kết mới ở argv.
//Nếu không: i giữ nguyên là 1. Đường dẫn đích sẽ ở argv và tên liên kết ở argv.
  if(argc > 1 && strcmp(argv[1], "-s") == 0){
    sflag = 1;
    i = 2;
  }

  if(argc - i != 2){
    fprintf(2, "Usage: ln [-s] old new\n");
    exit(1);
  }
 // Chương trình đảm bảo rằng sau khi đã bỏ qua các cờ (flags), người dùng phải cung cấp chính xác hai đối số: tệp nguồn (old hoặc target) và tên liên kết mới (new hoặc path).
//Nếu không đúng số lượng, nó sẽ in hướng dẫn sử dụng ra luồng lỗi tiêu chuẩn (file descriptor 2) và thoát











  //Trường hợp Tạo Liên kết Biểu tượng (sflag == 1):
//Chương trình gọi lời gọi hệ thống symlink(target, path). Lệnh này sẽ yêu cầu nhân tạo một Inode loại T_SYMLINK và lưu chuỗi đường dẫn đích vào các khối dữ liệu của nó.
//Trường hợp Tạo Liên kết Cứng (sflag == 0):
//Hệ thống gọi lời gọi hệ thống truyền thống link(old, new). Kết quả là tạo ra một mục nhập thư mục mới trỏ đến cùng một số hiệu Inode với tệp hiện có.

  if(sflag){
    if(symlink(argv[i], argv[i+1]) < 0){
      fprintf(2, "ln: symlink(%s, %s) failed\n", argv[i], argv[i+1]);
      exit(1);
    }
  } else {
    if(link(argv[i], argv[i+1]) < 0){
      fprintf(2, "ln: link(%s, %s) failed\n", argv[i], argv[i+1]);
      exit(1);
    }
  }
  exit(0);
}
