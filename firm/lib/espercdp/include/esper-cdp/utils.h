#pragma once
#include <strings.h>

char *rfc822_binary(void *src, unsigned long srcl, size_t *len);
const std::string urlEncode(const std::string& src);
