
#pragma once

#include <stdint.h>

#include <etl/optional.h>

struct CprPosition
{
    float lat;
    float lon;
};

etl::optional<CprPosition> decodeCPR(bool fflag, uint32_t even_cprlat, uint32_t even_cprlon,
                                     uint32_t odd_cprlat, uint32_t odd_cprlon);
