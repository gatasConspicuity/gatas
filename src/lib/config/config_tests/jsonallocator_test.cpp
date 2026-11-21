#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

#include "jsonallocator.hpp"

TEST_CASE("JSON allocator resizes through the FreeRTOS heap", "[json-allocator]")
{
    FreeRTOSJsonAllocator allocator;
    const auto allocations = mockPortMallocCount;
    const auto frees = mockPortFreeCount;
    auto *original = static_cast<uint8_t *>(allocator.reallocate(nullptr, 16));
    REQUIRE(original != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(original) % alignof(std::max_align_t) == 0);
    for (size_t i = 0; i < 16; ++i)
    {
        original[i] = static_cast<uint8_t>(i);
    }
    REQUIRE(allocator.reallocate(original, 8) == original);
    auto *grown = static_cast<uint8_t *>(allocator.reallocate(original, 64));
    REQUIRE(grown != nullptr);
    for (size_t i = 0; i < 16; ++i)
    {
        REQUIRE(grown[i] == i);
    }
    REQUIRE(allocator.reallocate(grown, 0) == nullptr);
    REQUIRE(mockPortMallocCount - allocations == 2);
    REQUIRE(mockPortFreeCount - frees == 2);
}

TEST_CASE("Failed JSON growth preserves the original allocation", "[json-allocator]")
{
    struct FailureScope
    {
        ~FailureScope() { mockPortMallocFail = false; }
    } scope;
    FreeRTOSJsonAllocator allocator;
    auto *original = static_cast<uint8_t *>(allocator.allocate(16));
    REQUIRE(original != nullptr);
    original[0] = 42;
    mockPortMallocFail = true;
    REQUIRE(allocator.reallocate(original, 64) == nullptr);
    REQUIRE(original[0] == 42);
    allocator.deallocate(original);
    allocator.deallocate(nullptr);
    REQUIRE(allocator.allocate(0) == nullptr);
    REQUIRE(allocator.allocate(std::numeric_limits<size_t>::max()) == nullptr);
}

TEST_CASE("ArduinoJson parsing and destruction use the FreeRTOS allocator", "[json-allocator]")
{
    FreeRTOSJsonAllocator allocator;
    const auto allocations = mockPortMallocCount;
    const auto frees = mockPortFreeCount;
    {
        ArduinoJson::JsonDocument doc{&allocator};
        REQUIRE(ArduinoJson::deserializeJson(doc, R"({"name":"OpenAce","values":[1,2,3]})") == ArduinoJson::DeserializationError::Ok);
        REQUIRE(doc["name"].as<ArduinoJson::JsonString>() == "OpenAce");
        REQUIRE(doc["values"][2].as<int>() == 3);
        doc.shrinkToFit();
        REQUIRE(mockPortMallocCount > allocations);
    }
    REQUIRE(mockPortMallocCount - allocations == mockPortFreeCount - frees);
}
