uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;
  int n;

  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  n = strlen(target);

  begin_op();
  // Tạo inode mới với loại T_SYMLINK
  if ((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
    end_op();
    return -1;
  }

  // Ghi đường dẫn đích vào các data blocks của inode
  // Lưu ý: Ghi n bytes của chuỗi target
  if (writei(ip, 0, (uint64)target, 0, n) != n) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->size = n; // Cập nhật kích thước inode bằng độ dài đường dẫn
  iupdate(ip);  // Ghi thay đổi inode xuống đĩa

  iunlockput(ip);
  end_op();

  return 0;
}
