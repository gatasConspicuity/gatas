#pragma once

#include <cstdint>

struct ip_addr_t
{
    uint32_t addr = 0;
};

#define IPADDR4_INIT(value) {value}
#define IP_GET_TYPE(address) 0
#define IPADDR_TYPE_V4 0
#define IP_ANY_TYPE nullptr
