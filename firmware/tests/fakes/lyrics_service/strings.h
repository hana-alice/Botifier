#pragma once
#ifdef _MSC_VER
#include <cstring>
#define strcasecmp _stricmp
#else
#include_next <strings.h>
#endif
