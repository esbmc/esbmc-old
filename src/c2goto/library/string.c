#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#undef strcpy
#undef strncpy
#undef strcat
#undef strncat
#undef strlen
#undef strcmp
#undef strncmp
#undef strchr
#undef strrchr
#undef strspn
#undef strcspn
#undef strpbrk
#undef strstr
#undef strtok
#undef memchr
#undef memcmp
#undef memset
#undef memcpy
#undef memmove
#undef memchr

char *strcpy(char *dst, const char *src)
{
__ESBMC_HIDE:;
  size_t i = 0;
  char ch;
  do
  {
    ch = src[i];
    dst[i] = ch;
    i++;
  } while(ch != (char)0);
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
__ESBMC_HIDE:;
  char *start = dst;

  while(n && (*dst++ = *src++))
    n--;

  if(n)
    while(--n)
      *dst++ = '\0';

  return start;
}

char *strcat(char *dst, const char *src)
{
__ESBMC_HIDE:;
  strcpy(dst + strlen(dst), src);
  return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
__ESBMC_HIDE:;
  char *start = dst;

  while(*dst++)
    ;
  dst--;

  while(n--)
    if(!(*dst++ = *src++))
      return start;

  *dst = '\0';
  return start;
}

size_t strlen(const char *s)
{
__ESBMC_HIDE:;
  size_t len = 0;
  while(s[len] != 0)
    len++;
  return len;
}

int strcmp(const char *p1, const char *p2)
{
__ESBMC_HIDE:;
  const unsigned char *s1 = (const unsigned char *)p1;
  const unsigned char *s2 = (const unsigned char *)p2;
  unsigned char c1, c2;

  do
  {
    c1 = (unsigned char)*s1++;
    c2 = (unsigned char)*s2++;
    if(c1 == '\0')
      return c1 - c2;
  } while(c1 == c2);

  return c1 - c2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
__ESBMC_HIDE:;
  size_t i = 0;
  unsigned char ch1, ch2;
  do
  {
    ch1 = s1[i];
    ch2 = s2[i];

    if(ch1 == ch2)
    {
    }
    else if(ch1 < ch2)
      return -1;
    else
      return 1;

    i++;
  } while(ch1 != 0 && ch2 != 0 && i < n);
  return 0;
}

char *strchr(const char *s, int ch)
{
__ESBMC_HIDE:;
  while(*s && *s != (char)ch)
    s++;
  if(*s == (char)ch)
    return (char *)s;
  return NULL;
}

char *strrchr(const char *s, int c)
{
__ESBMC_HIDE:;
  const char *found, *p;

  c = (unsigned char)c;

  /* Since strchr is fast, we use it rather than the obvious loop.  */

  if(c == '\0')
    return strchr(s, '\0');

  found = NULL;
  while((p = strchr(s, c)) != NULL)
  {
    found = p;
    s = p + 1;
  }

  return (char *)found;
}

size_t strspn(const char *s, const char *accept)
{
__ESBMC_HIDE:;
  const char *p;
  const char *a;
  size_t count = 0;

  for(p = s; *p != '\0'; ++p)
  {
    for(a = accept; *a != '\0'; ++a)
      if(*p == *a)
        break;
    if(*a == '\0')
      return count;
    else
      ++count;
  }

  return count;
}

size_t strcspn(const char *s, const char *reject)
{
__ESBMC_HIDE:;
  size_t count = 0;

  while(*s != '\0')
    if(strchr(reject, *s++) == NULL)
      ++count;
    else
      return count;

  return count;
}

char *strpbrk(const char *s, const char *accept)
{
__ESBMC_HIDE:;
  while(*s != '\0')
  {
    const char *a = accept;
    while(*a != '\0')
      if(*a++ == *s)
        return (char *)s;
    ++s;
  }

  return NULL;
}

char *strstr(const char *str1, const char *str2)
{
__ESBMC_HIDE:;
  char *cp = (char *)str1;
  char *s1, *s2;

  if(!*str2)
    return (char *)str1;

  while(*cp)
  {
    s1 = cp;
    s2 = (char *)str2;

    while(*s1 && *s2 && !(*s1 - *s2))
      s1++, s2++;
    if(!*s2)
      return cp;
    cp++;
  }

  return NULL;
}

char *strtok(char *str, const char *delim)
{
__ESBMC_HIDE:;
  static char *p = 0;
  if(str)
    p = str;
  else if(!p)
    return 0;
  str = p + strspn(p, delim);
  p = str + strcspn(str, delim);
  if(p == str)
    return p = 0;
  p = *p ? *p = 0, p + 1 : 0;
  return str;
}

char *strdup(const char *str)
{
__ESBMC_HIDE:;
  size_t bufsz;
  bufsz = (strlen(str) + 1);
  char *cpy = (char *)malloc(bufsz * sizeof(char));
  if(cpy == ((void *)0))
    return 0;
  strcpy(cpy, str);
  return cpy;
}

void *memcpy(void *dst, const void *src, size_t n)
{
__ESBMC_HIDE:;
  char *cdst = dst;
  const char *csrc = src;
  for(size_t i = 0; i < n; i++)
    cdst[i] = csrc[i];
  return dst;
}

void *__memset_impl(void *s, int c, size_t n)
{
__ESBMC_HIDE:;
  char *sp = s;
  for(size_t i = 0; i < n; i++)
    sp[i] = c;
  return s;
}

void *memset(void *s, int c, size_t n)
{
__ESBMC_HIDE:;
  void *hax = &__memset_impl;
  (void)hax;
  return __ESBMC_memset(s, c, n);
}

void *memmove(void *dest, const void *src, size_t n)
{
__ESBMC_HIDE:;
  char *cdest = dest;
  const char *csrc = src;
  if(dest - src >= n)
  {
    for(size_t i = 0; i < n; i++)
      cdest[i] = csrc[i];
  }
  else
  {
    for(size_t i = n; i > 0; i--)
      cdest[i - 1] = csrc[i - 1];
  }
  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
__ESBMC_HIDE:;
  int res = 0;
  const unsigned char *sc1 = s1, *sc2 = s2;
  for(; n != 0; n--)
  {
    res = (*sc1++) - (*sc2++);
    if(res != 0)
      return res;
  }
  return res;
}

void *memchr(const void *buf, int ch, size_t n)
{
  while(n && (*(unsigned char *)buf != (unsigned char)ch))
  {
    buf = (unsigned char *)buf + 1;
    n--;
  }

  return (n ? (void *)buf : NULL);
}
