// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#define MAX_RECURSION 10
#define min(a, b) ((a) < (b) ? (a) : (b))

struct superblock sb; 

static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
  ireclaim(dev);
}

static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){
        bp->data[bi/8] |= m;
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}

static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)
      empty = ip;
  }

  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    acquiresleep(&ip->lock);
    release(&itable.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);
    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

void
ireclaim(int dev)
{
  for (int inum = 1; inum < sb.ninodes; inum++) {
    struct inode *ip = 0;
    struct buf *bp = bread(dev, IBLOCK(inum, sb));
    struct dinode *dip = (struct dinode *)bp->data + inum % IPB;
    if (dip->type != 0 && dip->nlink == 0) {
      
      ip = iget(dev, inum);
    }
    brelse(bp);
    if (ip) {
      begin_op();
      ilock(ip);
      iunlock(ip);
      iput(ip);
      end_op();
    }
  }
}

static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  iupdate(ip);

  return tot;
}

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;

  return 0;
}

static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}
// fs.c — thay thế toàn bộ hàm namex và 2 hàm wrapper
//path resolution vad modify
static struct inode*
namex(char *path, int nameiparent, char *name, int follow)
{
  struct inode *ip, *next, *parent_dir;
  int depth = 0;
  char pathbuf[MAXPATH];  // 128 bytes
  char target[MAXPATH];   // 128 bytes
  parent_dir = 0; 
  // KHÔNG dùng combined[] nữa — tái dùng pathbuf để tiết kiệm stack
  //Thay vì sử dụng bộ nhớ tĩnh (static) dễ gây ra lỗi Race Condition hoặc tạo thêm mảng lớn gây tốn Stack,
//mã nguồn tái sử dụng pathbuf để lưu trữ đường dẫn đang xử lý
  strncpy(pathbuf, path, MAXPATH - 1);
  pathbuf[MAXPATH - 1] = '\0';
  path = pathbuf;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);
  //Modify Lookup
// Khởi tạo điểm bắt đầu: Hệ thống kiểm tra ký tự đầu tiên của path. 
// Nếu là /, việc tra cứu bắt đầu từ Inode gốc (ROOTINO). Nếu không, nó bắt đầu từ thư mục làm việc hiện tại (cwd) của tiến trình






//Vòng lặp while sử dụng skipelem để tách từng mắt xích của đường dẫn (ví dụ: tách a từ /a/b/c). Trong mỗi vòng lặp:

    //Kiểm tra loại thư mục: Chỉ Inode loại T_DIR mới được phép tiếp tục tra cứu thành phần con. 
//Nếu gặp loại khác, hàm giải phóng tài nguyên và trả về lỗi (FIX 1).
    //Xử lý nameiparent: Nếu cờ này được bật và đã đến mắt xích cuối cùng, hàm sẽ dừng lại và trả về thư mục cha (
//(dùng cho lệnh link hoặc unlink tác động lên chính cái tên tệp).
//parent_dir: Hệ thống giữ lại Inode của thư mục cha (idup(ip)). Điều này cực kỳ quan trọng để tuân thủ chuẩn POSIX:
//các liên kết tương đối (Relative Symlinks) phải được phân giải dựa trên thư mục chứa chính liên kết đó, chứ không phải cwd của tiến trình
  while((path = skipelem(path, name)) != 0){
    ilock(ip);

    if(ip->type != T_DIR){
      iunlockput(ip); //Trước khi thực hiện bước tiếp theo, hàm gọi iunlockput(ip) để nhả khóa Sleeplock của thư mục hiện tại. 
                     //Nếu giữ khóa thư mục cha trong khi đi tìm Inode con (vốn có thể là một liên kết trỏ ngược lại cha), hệ thống sẽ rơi vào tình trạng khóa chéo
      if(parent_dir){ iput(parent_dir); parent_dir = 0; }  // 
      return 0;
    }

    if(nameiparent && *path == '\0'){
      iunlock(ip);
      if(parent_dir){ iput(parent_dir); parent_dir = 0; }  // 
      return ip;
    }  

    if(parent_dir){ iput(parent_dir); parent_dir = 0; }    // giải phóng iteration trước

    parent_dir = idup(ip);
    next = dirlookup(ip, name, 0);
    iunlockput(ip);
    if(next == 0){
      iput(parent_dir); parent_dir = 0;
      return 0;
    }

    ip = next;
    ilock(ip);
 //Liên kết chỉ được "đuổi theo" nếu nó nằm ở giữa đường dẫn (*path != '\0') hoặc nếu tham số follow được bật (dành cho cat, exec)
//Biến depth tăng lên mỗi khi gặp một Symlink. Nếu depth > MAX_RECURSION (giới hạn 10), Kernel sẽ trả về lỗi để tránh treo máy do liên kết vòng
    if(ip->type == T_SYMLINK && (follow || *path != '\0')){
      if(++depth > MAX_RECURSION){
        iunlockput(ip);
        iput(parent_dir); parent_dir = 0;
        return 0;
      }
      int n = readi(ip, 0, (uint64)target, 0, MAXPATH - 1);
      if(n <= 0){
        iunlockput(ip);
        iput(parent_dir); parent_dir = 0;
        return 0;
      }
      target[n] = '\0';
      iunlockput(ip);



//Nối chuỗi: Sử dụng memmove để nối chuỗi target của Symlink vào phần còn lại của đường dẫn hiện tại ngay trong pathbuf.
//Tính liên tục: Lệnh continue cho phép vòng lặp quay lại từ đầu chuỗi vừa nối mà không cần khởi động lại toàn bộ quá trình tra cứu từ root, 
//giúp chi phí xử lý mỗi mắt xích là hằng số (O(n))
      if(*path != '\0'){
        int tlen = strlen(target); //Nội dung chuỗi đường dẫn đích được đọc từ các khối dữ liệu của Inode vào bộ đệm target
        int plen = strlen(path);
        if(tlen + 1 + plen + 1 > MAXPATH){
          iput(parent_dir); parent_dir = 0;
          return 0;
        }
//Nếu target là đường dẫn tuyệt đối (bắt đầu bằng /), Inode hiện tại được đặt lại là Root.
//Nếu là đường dẫn tương đối, Inode bắt đầu từ chính parent_dir (thư mục chứa link đó)
        memmove(pathbuf + tlen + 1, path, plen + 1);
        pathbuf[tlen] = '/';
        memmove(pathbuf, target, tlen);
      } else {
        memmove(pathbuf, target, strlen(target) + 1);
      }
      pathbuf[MAXPATH - 1] = '\0';
      path = pathbuf;

      if(pathbuf[0] == '/'){
        ip = iget(ROOTDEV, ROOTINO);
        iput(parent_dir); parent_dir = 0;
      } else {
        ip = parent_dir; parent_dir = 0;
      }
      continue;
    }

    iunlock(ip);
    // parent_dir được giải phóng ở đầu iteration tiếp
    // hoặc FIX 3 bên dưới nếu đây là iteration cuối
  }

  if(parent_dir){ iput(parent_dir); parent_dir = 0; }  // ✓ FIX 3

  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name, 1);
}

struct inode*
//hàm namei_nofollow được triển khai như một hàm bao (wrapper) cho hàm lõi namex, sử dụng tham số follow = 0
//để chỉ thị nhân dừng lại ngay tại mắt xích cuối cùng nếu đó là một liên kết biểu tượng
nameiparent(char *path, char *name)
{
  return namex(path, 1, name, 0);
}

struct inode*
namei_nofollow(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name, 0);
}
namei(path): Phân giải mọi mắt xích đến đích cuối cùng (follow = 1).
namei_nofollow(path): Dừng lại tại mắt xích cuối nếu nó là Symlink (follow = 0), dùng cho lệnh ls để xem thông tin link.
nameiparent(path, name): Dừng lại trước mắt xích cuối để thực hiện các thao tác tạo/xóa trên chính thực thể liên kết
