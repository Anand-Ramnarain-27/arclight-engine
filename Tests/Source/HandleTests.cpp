#include "Core/Handle.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace arc;

namespace
{
    struct Widget;
    using WidgetHandle = Handle<Widget>;
}

TEST_CASE("A default handle is null")
{
    const WidgetHandle handle;
    CHECK(handle.isNull());
    CHECK_FALSE(handle);
}

TEST_CASE("Freeing a slot makes every handle to it stale")
{
    HandleAllocator<Widget> allocator;
    const WidgetHandle first = *allocator.allocate();
    CHECK(allocator.isValid(first));

    CHECK(allocator.free(first));
    CHECK_FALSE(allocator.isValid(first));
    CHECK_FALSE(allocator.free(first));

    // The slot is reused, but with a new generation, so the old handle stays stale.
    const WidgetHandle second = *allocator.allocate();
    CHECK(second.index == first.index);
    CHECK(second.generation != first.generation);
    CHECK(allocator.isValid(second));
    CHECK_FALSE(allocator.isValid(first));
}

TEST_CASE("An allocator stops at its capacity")
{
    HandleAllocator<Widget> allocator(2);
    const auto a = allocator.allocate();
    const auto b = allocator.allocate();
    REQUIRE(a);
    REQUIRE(b);
    CHECK_FALSE(allocator.allocate());
    CHECK(allocator.aliveCount() == 2);

    allocator.free(*a);
    CHECK(allocator.allocate());
}

TEST_CASE("handleAt returns the live handle of a slot, or null")
{
    HandleAllocator<Widget> allocator;
    const WidgetHandle handle = *allocator.allocate();
    CHECK(allocator.handleAt(handle.index) == handle);
    allocator.free(handle);
    CHECK(allocator.handleAt(handle.index).isNull());
    CHECK(allocator.handleAt(99).isNull());
}

TEST_CASE("A pool returns values only through live handles")
{
    HandlePool<std::string, Widget> pool;
    const WidgetHandle a = pool.insert("a");
    const WidgetHandle b = pool.insert("b");
    REQUIRE(pool.get(a));
    CHECK(*pool.get(a) == "a");
    CHECK(pool.size() == 2);

    CHECK(pool.remove(a));
    CHECK(pool.get(a) == nullptr);
    CHECK_FALSE(pool.remove(a));
    CHECK(pool.size() == 1);

    const WidgetHandle c = pool.insert("c");
    CHECK(c.index == a.index);
    CHECK(pool.get(a) == nullptr);
    CHECK(*pool.get(c) == "c");

    std::vector<std::string> visited;
    pool.forEach([&](WidgetHandle, const std::string& value) { visited.push_back(value); });
    CHECK(visited == std::vector<std::string>{"c", "b"});
    CHECK(*pool.get(b) == "b");
}
