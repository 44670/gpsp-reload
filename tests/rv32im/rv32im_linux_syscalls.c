/* Minimal Linux syscall bridge for the bare-metal RV32 newlib toolchain.
 *
 * The ESP RV32 compiler supplies a useful freestanding newlib, but its
 * libgloss _open() uses the obsolete generic syscall number. qemu-user runs
 * a Linux ABI and requires openat(). The remaining libgloss calls already use
 * the generic Linux RISC-V syscall ABI, so this file only supplies the gaps
 * needed by the full gpSP runner and executable JIT mappings.
 */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RV32_LINUX_AT_FDCWD (-100)
#define RV32_LINUX_SYS_CLOSE 57
#define RV32_LINUX_SYS_OPENAT 56
#define RV32_LINUX_SYS_READ 63
#define RV32_LINUX_SYS_WRITE 64
#define RV32_LINUX_SYS_MUNMAP 215
#define RV32_LINUX_SYS_MMAP 222
#define RV32_LINUX_SYS_RISCV_FLUSH_ICACHE 259

#define RV32_LINUX_O_WRONLY 0x0001
#define RV32_LINUX_O_RDWR   0x0002
#define RV32_LINUX_O_CREAT  0x0040
#define RV32_LINUX_O_EXCL   0x0080
#define RV32_LINUX_O_TRUNC  0x0200
#define RV32_LINUX_O_APPEND 0x0400

static long rv32_linux_syscall3(long number, long arg0, long arg1, long arg2)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a7)
                   : "memory");
  return a0;
}

static long rv32_linux_syscall4(long number, long arg0, long arg1, long arg2,
                                long arg3)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a7)
                   : "memory");
  return a0;
}

static long rv32_linux_syscall6(long number, long arg0, long arg1, long arg2,
                                long arg3, long arg4, long arg5)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                     "r"(a7)
                   : "memory");
  return a0;
}

static long rv32_linux_result(long result)
{
  if (result < 0 && result >= -4095)
  {
    errno = (int)-result;
    return -1;
  }
  return result;
}

static int rv32_linux_open_flags(int flags)
{
  int linux_flags = 0;

  if ((flags & O_ACCMODE) == O_WRONLY)
    linux_flags |= RV32_LINUX_O_WRONLY;
  else if ((flags & O_ACCMODE) == O_RDWR)
    linux_flags |= RV32_LINUX_O_RDWR;
  if (flags & O_CREAT)
    linux_flags |= RV32_LINUX_O_CREAT;
  if (flags & O_EXCL)
    linux_flags |= RV32_LINUX_O_EXCL;
  if (flags & O_TRUNC)
    linux_flags |= RV32_LINUX_O_TRUNC;
  if (flags & O_APPEND)
    linux_flags |= RV32_LINUX_O_APPEND;
  return linux_flags;
}

int _open(const char *path, int flags, int mode)
{
  return (int)rv32_linux_result(rv32_linux_syscall4(
    RV32_LINUX_SYS_OPENAT, RV32_LINUX_AT_FDCWD, (long)path,
    rv32_linux_open_flags(flags), mode));
}

int _close(int fd)
{
  return (int)rv32_linux_result(rv32_linux_syscall3(
    RV32_LINUX_SYS_CLOSE, fd, 0, 0));
}

ssize_t _read(int fd, void *buffer, size_t length)
{
  return (ssize_t)rv32_linux_result(rv32_linux_syscall3(
    RV32_LINUX_SYS_READ, fd, (long)buffer, (long)length));
}

ssize_t _write(int fd, const void *buffer, size_t length)
{
  return (ssize_t)rv32_linux_result(rv32_linux_syscall3(
    RV32_LINUX_SYS_WRITE, fd, (long)buffer, (long)length));
}

/* qemu-riscv32 follows the 32-bit asm-generic stat ABI, where syscall 80 is
 * unavailable.  gpSP only needs stdio's descriptor classification here; ROM
 * sizing is performed by reading to EOF. */
int _fstat(int fd, struct stat *status)
{
  unsigned char *bytes = (unsigned char *)status;
  size_t index;

  if (!status || fd < 0)
  {
    errno = EINVAL;
    return -1;
  }
  for (index = 0; index < sizeof(*status); index++)
    bytes[index] = 0;
  status->st_mode = fd <= 2 ? S_IFCHR : S_IFREG;
  status->st_blksize = 4096;
  return 0;
}

off_t _lseek(int fd, off_t offset, int whence)
{
  (void)fd;
  (void)offset;
  (void)whence;
  errno = ESPIPE;
  return (off_t)-1;
}

int _stat(const char *path, struct stat *status)
{
  int fd = _open(path, O_RDONLY, 0);
  int result;

  if (fd < 0)
    return -1;
  result = _fstat(fd, status);
  _close(fd);
  return result;
}

void *mmap(void *address, size_t length, int prot, int flags, int fd,
           off_t offset)
{
  long result = rv32_linux_syscall6(
    RV32_LINUX_SYS_MMAP, (long)address, (long)length, prot, flags, fd,
    (long)offset);

  if (result < 0 && result >= -4095)
  {
    errno = (int)-result;
    return MAP_FAILED;
  }
  return (void *)result;
}

int munmap(void *address, size_t length)
{
  return (int)rv32_linux_result(rv32_linux_syscall3(
    RV32_LINUX_SYS_MUNMAP, (long)address, (long)length, 0));
}

void __clear_cache(void *start, void *end)
{
  (void)rv32_linux_syscall3(RV32_LINUX_SYS_RISCV_FLUSH_ICACHE,
                            (long)start, (long)end, 0);
}
