#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/*
  uint == uint256_t
  bytes == uint256_t
  bytes32 == uint256_t
  address == address_t
*/
typedef _ExtInt(256) int256_t;
typedef unsigned _ExtInt(256) uint256_t;
typedef unsigned _ExtInt(160) address_t;

/// Variables
// the value of these variables need to be set to rand afterwards
// sol_msg
uint256_t msg_data;
address_t msg_sender;
__uint32_t msg_sig;
uint256_t msg_value;

// sol_tx
uint256_t tx_gasprice;
address_t tx_origin;

// sol_block
uint256_t block_basefee;
uint256_t block_chainid;
address_t block_coinbase;
uint256_t block_difficulty;
uint256_t block_gaslimit;
uint256_t block_number;
uint256_t block_prevrandao;
uint256_t block_timestamp;

/// functions
// if the function does not currently have an actual implement,
// leave the params empty.
// blockhash
uint256_t blockhash();

// gasleft
uint256_t gasleft();

// abi
uint256_t abi_encode();
uint256_t abi_encodePacked();
uint256_t abi_encodeWithSelector();
uint256_t abi_encodeWithSignature();
uint256_t abi_encodeCall();

// math
uint256_t addmod(uint256_t x, uint256_t y, uint256_t k);
uint256_t mulmod(uint256_t x, uint256_t y, uint256_t k);
uint256_t keccak256();
uint256_t sha256();
address_t ripemd160();
address_t ecrecover();

// string
char* string_concat(char *x, char *y);

// bytes
void byte_concat();

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
}map_node_t;

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

void map_init_int(map_int_t *m);

void map_init_uint(map_uint_t *m);
void map_init_string(map_string_t *m);

void map_init_bool(map_bool_t *m);

void map_set_int(map_int_t *m, const char *key, const int value);
void map_set_uint(map_uint_t *m, const char *key, const unsigned int value);
void map_set_string(map_string_t *m, const char *key, char *value);
void map_set_bool(map_bool_t *m, const char *key, const bool value);
int *map_get_int(map_int_t *m, const char *key);
unsigned int *map_get_uint(map_uint_t *m, const char *key);
char **map_get_string(map_string_t *m, const char *key);
bool *map_get_bool(map_bool_t *m, const char *key);
unsigned map_hash(const char *str);
map_node_t *map_newnode(const char *key, void *value, int vsize);
int map_bucketidx(map_base_t *m, unsigned hash);
void map_addnode(map_base_t *m, map_node_t *node);
int map_resize(map_base_t *m, int nbuckets);
map_node_t **map_getref(map_base_t *m, const char *key);
void *map_get_(map_base_t *m, const char *key);
int map_set_(map_base_t *m, const char *key, void *value, int vsize);
void map_remove_(map_base_t *m, const char *key);
char get_char(int digit);
void rev(char *p);
char *i256toa(int256_t value);
char *u256toa(uint256_t value);