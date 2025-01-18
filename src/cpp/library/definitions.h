#ifndef STL_DEFINITIONS
#define STL_DEFINITIONS

#define SIGINT 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifndef __TIMESTAMP__
#  define __TIMESTAMP__ (0)
#endif

#ifdef _WIN64
typedef __int64 streamsize;
#else
typedef unsigned int streamsize;
#endif

class smanip
{
};

#define _SIZE_T_DEFINED

#endif
