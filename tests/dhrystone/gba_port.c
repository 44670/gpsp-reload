#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static unsigned char malloc_arena[1024];
static size_t malloc_used;
static long fake_time_ticks;

char *malloc(unsigned size)
{
  size_t aligned = (size + 3u) & ~3u;
  char *ptr;

  if (malloc_used + aligned > sizeof(malloc_arena))
    return 0;

  ptr = (char *)&malloc_arena[malloc_used];
  malloc_used += aligned;
  return ptr;
}

void *memcpy(void *dest, const void *src, size_t len)
{
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  while (len--)
    *d++ = *s++;

  return dest;
}

void *memset(void *dest, int value, size_t len)
{
  unsigned char *d = (unsigned char *)dest;

  while (len--)
    *d++ = (unsigned char)value;

  return dest;
}

char *strcpy(char *dest, const char *src)
{
  char *out = dest;

  while ((*dest++ = *src++) != '\0')
    ;

  return out;
}

int strcmp(const char *lhs, const char *rhs)
{
  while (*lhs && (*lhs == *rhs))
  {
    lhs++;
    rhs++;
  }

  return (unsigned char)*lhs - (unsigned char)*rhs;
}

int printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  va_end(ap);
  return 0;
}

int scanf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  va_end(ap);
  return 0;
}

long time(long *unused)
{
  fake_time_ticks++;
  if (unused)
    *unused = fake_time_ticks;
  return fake_time_ticks;
}
