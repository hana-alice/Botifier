#pragma once
#include <cstddef>
using socklen_t = unsigned;
struct timeval { long tv_sec; long tv_usec; };
inline constexpr int SOL_SOCKET = 1, SO_RCVTIMEO = 2;
#define getsockopt botifier_test_getsockopt
#define setsockopt botifier_test_setsockopt
int botifier_test_getsockopt(int socket, int level, int option, void* value, socklen_t* size);
int botifier_test_setsockopt(int socket, int level, int option, const void* value, socklen_t size);
