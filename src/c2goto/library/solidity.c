
#include <solidity.h>

uint256_t addmod(uint256_t x, uint256_t y, uint256_t k)
{
  return (x + y) % k;
}

uint256_t mulmod(uint256_t x, uint256_t y, uint256_t k)
{
  return (x * y) % k;
}

char *string_concat(char *x, char *y)
{
  strcat(x, y);
  return x;
}

/// Init
void map_init_int(map_int_t *m)
{
  memset(m, 0, sizeof(*(m)));
}

void map_init_uint(map_uint_t *m)
{
  memset(m, 0, sizeof(*(m)));
}

void map_init_string(map_string_t *m)
{
  memset(m, 0, sizeof(*(m)));
}

void map_init_bool(map_bool_t *m)
{
  memset(m, 0, sizeof(*(m)));
}

/// Set
void map_set_int(map_int_t *m, const char *key, const int value)
{
  (m)->tmp = value;
  map_set_(&(m)->base, key, &(m)->tmp, sizeof((m)->tmp));
}
void map_set_uint(map_uint_t *m, const char *key, const unsigned int value)
{
  (m)->tmp = value;
  map_set_(&(m)->base, key, &(m)->tmp, sizeof((m)->tmp));
}
void map_set_string(map_string_t *m, const char *key, char *value)
{
  (m)->tmp = value;
  map_set_(&(m)->base, key, &(m)->tmp, sizeof((m)->tmp));
}
void map_set_bool(map_bool_t *m, const char *key, const bool value)
{
  (m)->tmp = value;
  map_set_(&(m)->base, key, &(m)->tmp, sizeof((m)->tmp));
}

/// Get
int *map_get_int(map_int_t *m, const char *key)
{
  (m)->ref = map_get_(&(m)->base, key);
  zero_int = 0;
  return (m)->ref != NULL ? (m)->ref : &zero_int;
}
unsigned int *map_get_uint(map_uint_t *m, const char *key)
{
  (m)->ref = map_get_(&(m)->base, key);
  zero_uint = 0;
  return (m)->ref != NULL ? (m)->ref : &zero_uint;
}
char **map_get_string(map_string_t *m, const char *key)
{
  (m)->ref = map_get_(&(m)->base, key);
  zero_string = "0";
  return (m)->ref != NULL ? (m)->ref : &zero_string;
}
bool *map_get_bool(map_bool_t *m, const char *key)
{
  (m)->ref = map_get_(&(m)->base, key);
  zero_bool = false;
  return (m)->ref != NULL ? (m)->ref : &zero_bool;
}

/// General
unsigned map_hash(const char *str)
{
  unsigned hash = 5381;
  if (str != NULL)
    while (*str)
    {
      hash = ((hash << 5) + hash) ^ *str;
      str++;
    }
  return hash;
}

map_node_t *map_newnode(const char *key, void *value, int vsize)
{
  map_node_t *node;
  int ksize = strlen(key) + 1;
  int voffset = ksize + ((sizeof(void *) - ksize) % sizeof(void *));
  node = malloc(sizeof(*node) + voffset + vsize);
  if (!node)
    return NULL;
  memcpy(node + 1, key, ksize);
  node->hash = map_hash(key);
  node->value = ((char *)(node + 1)) + voffset;
  memcpy(node->value, value, vsize);
  return node;
}

int map_bucketidx(map_base_t *m, unsigned hash)
{
  return hash & (m->nbuckets - 1);
}

void map_addnode(map_base_t *m, map_node_t *node)
{
  int n = map_bucketidx(m, node->hash);
  node->next = m->buckets[n];
  m->buckets[n] = node;
}

int map_resize(map_base_t *m, int nbuckets)
{
  map_node_t *nodes, *node, *next;
  map_node_t **buckets;
  int i;
  nodes = NULL;
  i = m->nbuckets;
  while (i--)
  {
    node = (m->buckets)[i];
    while (node)
    {
      next = node->next;
      node->next = nodes;
      nodes = node;
      node = next;
    }
  }
  /* Reset buckets */
  /* --force-malloc-success */
  buckets = malloc(sizeof(*m->buckets) * nbuckets);
  if (buckets != NULL)
  {
    m->buckets = buckets;
    m->nbuckets = nbuckets;
  }
  if (m->buckets)
  {
    memset(m->buckets, 0, sizeof(*m->buckets) * m->nbuckets);
    /* Re-add nodes to buckets */
    node = nodes;
    while (node)
    {
      next = node->next;
      map_addnode(m, node);
      node = next;
    }
  }
  /* Return error code if realloc() failed */
  /* --force-malloc-success */
  return 0;
}

map_node_t **map_getref(map_base_t *m, const char *key)
{
  unsigned hash = map_hash(key);
  map_node_t **next;
  if (m->nbuckets > 0)
  {
    next = &m->buckets[map_bucketidx(m, hash)];
    while (*next)
    {
      if ((*next)->hash == hash && !strcmp((char *)(*next + 1), key))
      {
        return next;
      }
      next = &(*next)->next;
    }
  }
  return NULL;
}

void *map_get_(map_base_t *m, const char *key)
{
  map_node_t **next = map_getref(m, key);
  return next ? (*next)->value : NULL;
}

int map_set_(map_base_t *m, const char *key, void *value, int vsize)
{
  int n, err;
  map_node_t **next, *node;
  next = map_getref(m, key);
  if (next)
  {
    memcpy((*next)->value, value, vsize);
    return 0;
  }
  node = map_newnode(key, value, vsize);
  if (node == NULL)
    return -1;
  if (m->nnodes >= m->nbuckets)
  {
    n = (m->nbuckets > 0) ? (m->nbuckets << 1) : 1;
    err = map_resize(m, n);
    if (err)
      return -1;
  }
  map_addnode(m, node);
  m->nnodes++;
  return 0;
}

void map_remove_(map_base_t *m, const char *key)
{
  map_node_t *node;
  map_node_t **next = map_getref(m, key);
  if (next)
  {
    node = *next;
    *next = (*next)->next;
    free(node);
    m->nnodes--;
  }
}

char get_char(int digit)
{
  char charstr[] = "0123456789ABCDEF";
  return charstr[digit];
}

void rev(char *p)
{
  char *q = &p[strlen(p) - 1];
  char *r = p;
  for (; q > r; q--, r++)
  {
    char s = *q;
    *q = *r;
    *r = s;
  }
}

char *i256toa(int256_t value)
{
  // we might have memory leak as we will not free this afterwards
  char *str = malloc(256 * sizeof(char));
  int256_t base = (int256_t)10;
  unsigned short count = 0;
  bool flag = true;

  if (value < (int256_t)0 && base == (int256_t)10)
  {
    flag = false;
  }
  if (value == (int256_t)0)
  {
    str = "0";
    return str;
  }
  while (value != (int256_t)0)
  {
    int256_t dig = value % base;
    value -= dig;
    value /= base;

    if (flag == true)
      str[count] = get_char(dig);
    else
      str[count] = get_char(-dig);
    count++;
  }
  if (flag == false)
  {
    str[count] = '-';
    count++;
  }
  str[count] = 0;
  rev(str);
  return str;
}

char *u256toa(uint256_t value)
{
  char *str = malloc(256 * sizeof(char));
  uint256_t base = (int256_t)10;
  unsigned int count = 0;
  bool flag = true;
  if (value < (uint256_t)0 && base == (uint256_t)10)
  {
    flag = false;
  }
  if (value == (uint256_t)0)
  {
    str = "0";
    return str;
  }
  if (value != (uint256_t)0)
  {
    while (value != (uint256_t)0)
    {
      uint256_t dig = value % base;
      value -= dig;
      value /= base;

      if (flag == true)
        str[count] = get_char(dig);
      else
        str[count] = get_char(-dig);
      count++;
    }
    if (flag == false)
    {
      str[count] = '-';
      count++;
    }
  }
  str[count] = 0;
  rev(str);
  return str;
}
