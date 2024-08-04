/*
  The template/library for Solidity built-in variables, function and data structure
*/

#include <nlohmann/json.hpp>
#include <unordered_set>

#ifndef SOLIDITY_TEMPLATE_H_
#  define SOLIDITY_TEMPLATE_H_

namespace SolidityTemplate
{
/// header & typedef
const std::string sol_header = R"(
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
)";

/*
  uint == uint256_t
  bytes == uint256_t
  bytes32 == uint256_t
  address == address_t
*/
const std::string sol_typedef = R"(
typedef _ExtInt(256) int256_t;
typedef unsigned _ExtInt(256) uint256_t;
typedef unsigned _ExtInt(160) address_t;
struct _ESBMC_MAPPING_STRING
{
    char value[256]; // a string
};
)";

/// Variables
// the value of these variables need to be set to rand afterwards
const std::string sol_msg = R"(
uint256_t msg_data;
address_t msg_sender;
__uint32_t msg_sig;
uint256_t msg_value;
)";

const std::string sol_tx = R"(
uint256_t tx_gasprice;
address_t tx_origin;
)";

const std::string sol_block = R"(
uint256_t block_basefee;
uint256_t block_chainid;
address_t block_coinbase;
uint256_t block_difficulty;
uint256_t block_gaslimit;
uint256_t block_number;
uint256_t block_prevrandao;
uint256_t block_timestamp;
)";

const std::string sol_vars = sol_msg + sol_tx + sol_block;

/// functions
// if the function does not currently have an actual implement,
// leave the params empty.
const std::string blockhash = R"(
uint256_t blockhash();
)";

const std::string gasleft = R"(
uint256_t gasleft();
)";

const std::string sol_abi = R"(
uint256_t abi_encode();
uint256_t abi_encodePacked();
uint256_t abi_encodeWithSelector();
uint256_t abi_encodeWithSignature();
uint256_t abi_encodeCall();
)";

const std::string sol_math = R"(
uint256_t addmod(uint256_t x, uint256_t y, uint256_t k)
{
	return (x + y) % k;
}

uint256_t mulmod(uint256_t x, uint256_t y, uint256_t k)
{
	return (x * y) % k;
}

uint256_t keccak256();
uint256_t sha256();
address_t ripemd160();
address_t ecrecover();
)";

const std::string sol_string = R"(
char* string_concat(char *x, char *y)
{
	strcat(x, y);
	return x;
}
)";

const std::string sol_byte = R"(
void byte_concat();
)";

const std::string sol_funcs =
  blockhash + gasleft + sol_abi + sol_math + sol_string + sol_byte;

/// data structure

/* https://github.com/rxi/map */
const std::string sol_mapping = R"(
struct map_node_t;
typedef struct map_node_t map_node_t;

int zero_int;
unsigned int zero_uint;
bool zero_bool;
char *zero_string;

typedef struct map_base_t
{
	map_node_t **buckets;
	unsigned nbuckets, nnodes;
} map_base_t;

typedef struct map_iter_t
{
	unsigned bucketidx;
	map_node_t *node;
} map_iter_t;

typedef struct map_node_t
{
	unsigned hash;
	void *value;
	map_node_t *next;
} map_node_t;

void *map_get_(map_base_t *m, const char *key);
int map_set_(map_base_t *m, const char *key, void *value, int vsize);
void map_remove_(map_base_t *m, const char *key);

typedef struct map_int_t
{
	map_base_t base;
	int *ref;
	int tmp;
} map_int_t;

typedef struct map_uint_t
{
	map_base_t base;
	unsigned int *ref;
	unsigned int tmp;
} map_uint_t;

typedef struct map_string_t
{
	map_base_t base;
	char **ref;
	char *tmp;
} map_string_t;

typedef struct map_bool_t
{
	map_base_t base;
	bool *ref;
	bool tmp;
} map_bool_t;

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
	// avoid derefencing null ptr
	if (str != NULL)
		while (*str)
		{
			hash = ((hash << 5) + hash) ^ *str++;
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
)";

/// external library
// itoa
const std::string sol_itoa = R"(
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
	char *str = (char *)malloc(256 * sizeof(char));
	int256_t base = (int256_t)10;
	unsigned short count = 0;
	bool flag = true;

	if (value < (int256_t)0 && base == (int256_t)10)
	{
		flag = false;
	}
	if (value == (int256_t)0)
	{
		str[count] = '\0';
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
	char *str = (char *)malloc(256 * sizeof(char));
	uint256_t base = (uint256_t)10;
	unsigned short count = 0;
	if (value == (uint256_t)0)
	{
		str[count] = '\0';
		return str;
	}
	while (value != (uint256_t)0)
	{
		uint256_t dig = value % base;
		value -= dig;
		value /= base;
		str[count] = get_char(dig);
		count++;
	}
	str[count] = 0;
	rev(str);
	return str;
}
)";

// string2hex
const std::string sol_str2hex = R"(
static const long hextable[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
char *decToHexa(int n)
{
    char *hexaDeciNum = (char *)malloc(256 * sizeof(char));
    hexaDeciNum[0] = '\0';
    int i = 0;
    while (n != 0)
    {
        int temp = 0;
        temp = n % 16;
        if (temp < 10)
        {
            hexaDeciNum[i] = temp + 48;
            i++;
        }
        else
        {
            hexaDeciNum[i] = temp + 55;
            i++;
        }

        n /= 16;
    }
    char *ans = (char *)malloc(256 * sizeof(char));
    ans[0] = '\0';
    int pos = 0;
    for (int j = i - 1; j >= 0; j--)
    {
        ans[pos] = (char)hexaDeciNum[j];
        pos++;
    }
    ans[pos] = '\0';
    return ans;
}
char *ASCIItoHEX(char *ascii)
{
    char *hex = (char *)malloc(256 * sizeof(char));
    hex[0] = '\0';
    for (int i = 0; i < strlen(ascii); i++)
    {
        char ch = ascii[i];
        int tmp = (int)ch;
        char *part = decToHexa(tmp);
        strcat(hex, part);
    }
    return hex;
}
long hexdec(const char *hex)
{
    /*https://stackoverflow.com/questions/10324/convert-a-hexadecimal-string-to-an-integer-efficiently-in-c*/
    long ret = 0;
    while (*hex && ret >= 0)
    {
        ret = (ret << 4) | hextable[*hex++];
    }
    return ret;
}
long str2int(char *str)
{
    return hexdec(ASCIItoHEX(str));
}
)";

const std::string sol_ext_library = sol_itoa + sol_str2hex;

// combination
const std::string sol_library = sol_header + sol_typedef + sol_vars +
                                sol_funcs + sol_mapping + sol_ext_library;

}; // namespace SolidityTemplate

#endif
