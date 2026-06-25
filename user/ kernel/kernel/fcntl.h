#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800 //Được định nghĩa là 0x800 trong kernel/fcntl.h. Cờ này cho phép open() trả về chính Inode của cái link thay vì đi xuyên qua nó
