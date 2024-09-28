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
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <string.h>
#include <string>
#include <cstdbool>
#include <cassert>
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
struct NodeU
{
  uint256_t data : 256;
  struct NodeU *next;
};

struct NodeI
{
  int256_t data : 256;
  struct NodeI *next;
};

void insertAtEndU(struct NodeU **head, uint256_t data)
{
  struct NodeU *newNode = (struct NodeU *)malloc(sizeof(struct NodeU));
  newNode->data = data;
  newNode->next = NULL;
  if (*head == NULL)
  {
    *head = newNode;
    return;
  }
  struct NodeU *current = *head;
  while (current->next != NULL)
  {
    current = current->next;
  }
  current->next = newNode;
}

void insertAtEndI(struct NodeI **head, int256_t data)
{
  struct NodeI *newNode = (struct NodeI *)malloc(sizeof(struct NodeI));
  newNode->data = data;
  newNode->next = NULL;
  if (*head == NULL)
  {
    *head = newNode;
    return;
  }
  struct NodeI *current = *head;
  while (current->next != NULL)
  {
    current = current->next;
  }
  current->next = newNode;
}

int findKeyU(struct NodeU *head, uint256_t key)
{
  struct NodeU *current = head;
  int cnt = 0;
  while (current != NULL)
  {
    if (current->data == key)
      return cnt;
    cnt++;
    current = current->next;
  }
  insertAtEndU(&head, key);
  // temporary
  if (cnt >= 50)
    assert(0);
  return cnt;
}

int findKeyI(struct NodeI *head, int256_t key)
{
  struct NodeI *current = head;
  int cnt = 0;
  while (current != NULL)
  {
    if (current->data == key)
      return cnt;
    cnt++;
    current = current->next;
  }
  insertAtEndI(&head, key);
  // temporary
  if (cnt >= 50)
    assert(0);
  return cnt;
}
)";

const std::string sol_array = R"(
void *arrcpy(void *from_array, size_t from_size, size_t size_of)
{
  assert(from_size != 0);
  void *to_array = (void *)calloc(from_size, size_of);
  memcpy(to_array, from_array, from_size * size_of);
  return to_array;
}
)";

/// external library
// itoa
/* 
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
*/

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
char *ASCIItoHEX(const char *ascii)
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
uint256_t hexdec(const char *hex)
{
    /*https://stackoverflow.com/questions/10324/convert-a-hexadecimal-string-to-an-integer-efficiently-in-c*/
    uint256_t ret = 0;
    while (*hex && ret >= (uint256_t)0)
    {
        ret = (ret << (uint256_t)4) | (uint256_t)hextable[*hex++];
    }
    return ret;
}
uint256_t str2int(const char *str)
{
    return hexdec(ASCIItoHEX(str));
}
)";

const std::string sol_ext_library = sol_str2hex;

const std::string sol_c_library = "extern \"C\" {" + sol_typedef + sol_vars +
                                  sol_funcs + sol_mapping + sol_array +
                                  sol_ext_library + "}";

// For C++
const std::string sol_cpp_string = R"(
const std::string empty_str = "";
void _streq(std::string &str1, std::string str2)
{
  __ESBMC_assume(!str2.empty());
  str1 = str2;
}
std::string _tostr(const char* ptr)
{
  return std::string(ptr);
}
const char* _tochar(std::string str)
{
  return str.c_str();
}
)";

const std::string sol_cpp_library = sol_cpp_string;

// combination
const std::string sol_library = sol_header + sol_c_library + sol_cpp_library;

}; // namespace SolidityTemplate

#endif
