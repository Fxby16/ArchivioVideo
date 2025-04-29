#include "random.hpp"

#include <random>

namespace uint32_random{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
    uint32_t random_number = dist(gen);
}

uint32_t rand_uint32()
{
    return uint32_random::dist(uint32_random::gen);
}