#pragma once

#include <cstdint>

struct pbuf
{
    pbuf *next = nullptr;
    void *payload = nullptr;
    uint16_t len = 0;
};

inline void pbuf_free(pbuf *) {}
