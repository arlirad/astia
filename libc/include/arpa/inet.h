#pragma once

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

int       inet_aton(const char* text, struct in_addr* address);
in_addr_t inet_addr(const char* text);
in_addr_t inet_network(const char* text);
char*     inet_ntoa(struct in_addr address);

int         inet_pton(int family, const char* text, void* address);
const char* inet_ntop(int family, const void* address, char* text, socklen_t size);

#ifdef __cplusplus
}
#endif
