#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <ubuntu20.04/kernel_5.15.0-76/include/linux/slab.h>
#include <ubuntu20.04/kernel_5.15.0-76/include/asm/uaccess.h>

#include <assert.h>

#undef errno
extern _Thread_local int errno;

#undef exit
#undef abort
#undef calloc
#undef getenv
#undef atoi
#undef atol
#undef atoll

typedef struct atexit_key
{
  void (*atexit_func)();
  struct atexit_key *next;
} __ESBMC_atexit_key;

static __ESBMC_atexit_key *__ESBMC_atexits = NULL;

void __atexit_handler()
{
__ESBMC_HIDE:;
  // This is here to prevent k-induction from unwind the next loop unnecessarily
  if(__ESBMC_atexits == NULL)
    return;

  while(__ESBMC_atexits)
  {
    __ESBMC_atexits->atexit_func();
    __ESBMC_atexit_key *__ESBMC_tmp = __ESBMC_atexits->next;
    free(__ESBMC_atexits);
    __ESBMC_atexits = __ESBMC_tmp;
  }
}

int atexit(void (*func)(void))
{
__ESBMC_HIDE:;
  __ESBMC_atexit_key *l =
    (__ESBMC_atexit_key *)malloc(sizeof(__ESBMC_atexit_key));
  if(l == NULL)
    return -1;
  l->atexit_func = func;
  l->next = __ESBMC_atexits;
  __ESBMC_atexits = l;
  return 0;
}

#pragma clang diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-noreturn"
void exit(int status)
{
__ESBMC_HIDE:;
  __atexit_handler();
  __ESBMC_memory_leak_checks();
  __ESBMC_assume(0);
}

void abort(void)
{
__ESBMC_HIDE:;
  __ESBMC_memory_leak_checks();
  __ESBMC_assume(0);
}

void _Exit(int status)
{
__ESBMC_HIDE:;
  __ESBMC_memory_leak_checks();
  __ESBMC_assume(0);
}
#pragma clang diagnostic pop

void *calloc(size_t nmemb, size_t size)
{
__ESBMC_HIDE:;
  if(!nmemb)
    return NULL;

  size_t total_size = nmemb * size;
  void *res = malloc(total_size);
  if(res)
    memset(res, 0, total_size);
  return res;
}

long int strtol(const char *str, char **endptr, int base)
{
__ESBMC_HIDE:;
  long int result = 0;
  int sign = 1;

  // Handle whitespace
  while(isspace(*str))
    str++;

  // Handle sign
  if(*str == '-')
  {
    sign = -1;
    str++;
  }
  else if(*str == '+')
    str++;

  // Handle base
  if(base == 0)
  {
    if(*str == '0')
    {
      base = 8;
      if(tolower(str[1]) == 'x')
      {
        base = 16;
        str += 2;
      }
      else
        str++;
    }
    else
      base = 10;
  }
  else if(base == 16 && *str == '0' && tolower(str[1]) == 'x')
    str += 2;

  // Convert digits
  while(isdigit(*str) || (base == 16 && isxdigit(*str)))
  {
    int digit = tolower(*str) - '0';
    if(digit > 9)
      digit -= 7;
    if(result > (LONG_MAX - digit) / base)
      return sign == -1 ? LONG_MIN : LONG_MAX;
    result = result * base + digit;
    str++;
  }

  // Set end pointer
  if(endptr != NULL)
    *endptr = (char *)str;

  return sign * result;
}

/* one plus the numeric value, rest is zero */
static const unsigned char ATOI_MAP[256] = {
  ['0'] = 1,
  ['1'] = 2,
  ['2'] = 3,
  ['3'] = 4,
  ['4'] = 5,
  ['5'] = 6,
  ['6'] = 7,
  ['7'] = 8,
  ['8'] = 9,
  ['9'] = 10,
};

#define ATOI_DEF(name, type, TYPE)                                             \
  type name(const char *s)                                                     \
  {                                                                            \
  __ESBMC_HIDE:;                                                               \
    while(isspace(*s))                                                         \
      s++;                                                                     \
    int neg = 0;                                                               \
    if(*s == '-')                                                              \
    {                                                                          \
      neg = 1;                                                                 \
      s++;                                                                     \
    }                                                                          \
    else if(*s == '+')                                                         \
      s++;                                                                     \
    unsigned type r = 0;                                                       \
    for(unsigned char c; (c = ATOI_MAP[(unsigned char)*s]); s++)               \
    {                                                                          \
      c--;                                                                     \
      if(r > (TYPE##_MAX - c) / 10)                                            \
        return neg ? TYPE##_MIN : TYPE##_MAX;                                  \
      r *= 10;                                                                 \
      r += c;                                                                  \
    }                                                                          \
    return neg ? -r : r;                                                       \
  }

ATOI_DEF(atoi, int, INT)
ATOI_DEF(atol, long, LONG)
ATOI_DEF(atoll, long long, LLONG)

#undef ATOI_DEF

char *getenv(const char *name)
{
__ESBMC_HIDE:;

  _Bool found;
  if(!found)
    return 0;

  char *buffer;
  size_t buf_size;

  __ESBMC_assume(buf_size >= 1);
  buffer = (char *)malloc(buf_size);
  buffer[buf_size - 1] = 0;
  return buffer;
}

typedef unsigned int gfp_t;

static void check_gfp_flags(gfp_t flags)
{
  // Define all valid flags
  gfp_t valid_flags =
    __GFP_DMA | __GFP_HIGHMEM | __GFP_DMA32 | __GFP_ZERO | __GFP_NOWARN |
    __GFP_REPEAT | __GFP_NOFAIL | __GFP_NORETRY | __GFP_MEMALLOC | __GFP_COMP |
    __GFP_NO_KSWAPD | __GFP_OTHER_NODE | __GFP_WRITE | __GFP_HARDWALL |
    __GFP_THISNODE | __GFP_ATOMIC | __GFP_ACCOUNT | __GFP_RECLAIM | __GFP_IO |
    __GFP_FS | GFP_KERNEL | GFP_KERNEL_ACCOUNT | GFP_NOIO | GFP_NOFS |
    GFP_USER | GFP_DMA | GFP_DMA32 | GFP_HIGHUSER;

  // Check if any flag is set that is not in the list of valid flags
  assert((flags & ~valid_flags) == 0);
}
static void *__kmalloc(size_t size, gfp_t flags)
{
  return malloc(size);
}

static void *__kmalloc_large(size_t size, gfp_t flags)
{
  (void)flags; // Ignore flags.
  return malloc(size);
}

void *kmalloc(int size, int flags)
{
  // Check size greater than  zero and less than max
  assert(size > 0 && size <= MAX_ALLOC_SIZE);
  //check flags greater than zero
  assert(flags > 0);

  //check if flags have corresponding valid values
  check_gfp_flags(flags);
  // If the size is larger than the KMALLOC_MAX_CACHE_SIZE, then handle in kmalloc_large
  if(size > KMALLOC_MAX_CACHE_SIZE)
  {
    // Call to kmalloc_large or equivalent function can be here.
    return __kmalloc_large(size, flags);
  }

  (void)flags; // Ignore flags.
  return __kmalloc(size, flags);
}

void kfree(const void *ptr)
{
  free((void *)ptr);
}
unsigned long copy_to_user(void *to, void *from, unsigned size)
{
   //checking on the passed parameters of kernel function
   //the source in kernel space and destination in user space must be valid 
    assert(to != NULL);
    assert(from != NULL);
    assert(size <= PAGE_SIZE);

    //check on user space memory boundry
    assert((char*) to < user_memory + USER_MEMORY_SPACE  && (char*) to >= user_memory);
    //check on kernel space memory boundry
    assert((char*) from < kernel_memory + KERNEL_MEMORY_SPACE  && (char*) from >= kernel_memory);
    //copy memory from kernel space to user space
    //simulate the copy operation by memcpy 
    memcpy(to, from, size);

    return 0;
}

unsigned long copy_from_user(void * to, void * from, unsigned size)
{
    //the source in user space and destination in kernel space must be valid
    //avoid dereferencing null pointer
    assert(to != NULL);
    assert(from != NULL);
    assert(size <= PAGE_SIZE);

    //check on kernel space memory boundry
    assert((char*) to < kernel_memory + KERNEL_MEMORY_SPACE  && (char*) to >= kernel_memory);
    //check on user space memory boundry
    assert((char*) from < user_memory + USER_MEMORY_SPACE  && (char*) from >= user_memory);

    //copy memory from user space to kernel space
    //use user space function to mock 
    memcpy(to, from, size);

    return 0;
} 


void *ldv_malloc(size_t size)
{
  return malloc(size);
}

void *ldv_zalloc(size_t size)
{
  return malloc(size);
}

void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
  return __kmalloc(n * size, flags);
}

void *kcalloc(size_t n, size_t size, gfp_t flags)
{
  (void)flags;
  return calloc(n, size);
}

size_t strlcat(char *dst, const char *src, size_t siz)
{
  char *d = dst;
  const char *s = src;
  size_t n = siz;
  size_t dlen;

  /* Find the end of dst and adjust bytes left but don't go past end */
  while(n-- != 0 && *d != '\0')
    d++;
  dlen = d - dst;
  n = siz - dlen;

  if(n == 0)
    return (dlen + strlen(s));
  while(*s != '\0')
  {
    if(n != 1)
    {
      *d++ = *s;
      n--;
    }
    s++;
  }
  *d = '\0';

  return (dlen + (s - src)); /* count does not include NUL */
}

void *aligned_alloc(size_t align, size_t size)
{
__ESBMC_HIDE:;
  if(
    !align || (align & (align - 1)) || /* alignment must be a power of 2 */
    (size & (align - 1)) /* size must be a multiple of alignment */
  )
  {
    errno = EINVAL;
    return NULL;
  }
  void *r = malloc(size);
  __ESBMC_assume(!((uintptr_t)r & (align - 1)));
  return r;
}
