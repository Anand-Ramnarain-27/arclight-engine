#include "Platform/FileSystem.h"
#include "Platform/VirtualFileSystem.h"

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace arc;

namespace
{
    std::string text(const std::optional<std::vector<std::byte>>& bytes)
    {
        return bytes ? std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : std::string("<none>");
    }

    std::vector<std::string> names(const std::vector<DirectoryEntry>& entries)
    {
        std::vector<std::string> result;
        for (const DirectoryEntry& entry : entries)
            result.push_back(entry.name);
        return result;
    }

    // A fresh folder under the system temporary folder, removed with everything in it.
    struct TemporaryFolder
    {
        TemporaryFolder()
        {
            std::random_device random;
            path = std::filesystem::temp_directory_path() / ("arclight-tests-" + std::to_string(random()));
            std::filesystem::create_directories(path);
        }
        ~TemporaryFolder()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        [[nodiscard]] std::string utf8() const
        {
            const std::u8string u8 = path.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::filesystem::path path;
    };

    // Installs a file system for one test and puts the previous one back.
    struct ActiveFileSystem
    {
        explicit ActiveFileSystem(VirtualFileSystem& fileSystem) : previous(activeFileSystem())
        {
            setActiveFileSystem(&fileSystem);
        }
        ~ActiveFileSystem() { setActiveFileSystem(previous); }

        VirtualFileSystem* previous;
    };
}

TEST_CASE("Virtual paths are told apart from native ones")
{
    CHECK(isVirtualPath("assets://Models/a.glb"));
    CHECK(isVirtualPath("user:settings.ini"));
    CHECK(isVirtualPath("Assets://a"));
    CHECK_FALSE(isVirtualPath("C:\\Users\\anand\\a.glb"));
    CHECK_FALSE(isVirtualPath("c:/models/a.glb"));
    CHECK_FALSE(isVirtualPath("/Users/anand/a.glb"));
    CHECK_FALSE(isVirtualPath("Models/a.glb"));
    CHECK_FALSE(isVirtualPath("my folder:a"));
}

TEST_CASE("Virtual paths are normalised")
{
    const auto parse = [](std::string_view path) {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        return parsed ? parsed->toString() : std::string("<invalid>");
    };
    CHECK(parse("assets://Models/a.glb") == "assets://Models/a.glb");
    CHECK(parse("assets:/Models/a.glb") == "assets://Models/a.glb");
    CHECK(parse("assets:Models//./a.glb") == "assets://Models/a.glb");
    CHECK(parse("assets://Models\\Sub\\..\\a.glb") == "assets://Models/a.glb");
    CHECK(parse("assets://Models/") == "assets://Models");
    CHECK(parse("assets://") == "assets://");
    CHECK(parse("assets://Models/../..") == "<invalid>");
    CHECK(parse("assets://../outside.txt") == "<invalid>");
    CHECK(parse("assets://C:/Windows/win.ini") == "<invalid>");
    CHECK(parse("Assets://a") == "<invalid>");
    CHECK(parse("C:\\a") == "<invalid>");
}

TEST_CASE("Path helpers work on virtual and native paths")
{
    CHECK(joinPath("assets://", "a.glb") == "assets://a.glb");
    CHECK(joinPath("assets://Models", "a.glb") == "assets://Models/a.glb");
    CHECK(joinPath("C:\\Models\\", "a.glb") == "C:\\Models\\a.glb");
    CHECK(joinPath("", "a.glb") == "a.glb");

    CHECK(parentPath("assets://Models/a.glb") == "assets://Models");
    CHECK(parentPath("assets://a.glb") == "assets://");
    CHECK(parentPath("/home/anand/a.glb") == "/home/anand");
    CHECK(parentPath("C:\\Models\\a.glb") == "C:\\Models");

    CHECK(fileNameOf("assets://Models/a.glb") == "a.glb");
    CHECK(fileNameOf("C:\\Models\\a.glb") == "a.glb");
    CHECK(extensionOf("assets://Models/Lantern.GLB") == ".glb");
    CHECK(extensionOf("assets://Models/archive.tar.gz") == ".gz");
    CHECK(extensionOf("assets://.hidden") == "");
    CHECK(extensionOf("assets://Models/README") == "");
}

TEST_CASE("Later mounts override earlier ones and listings merge every layer")
{
    auto base = std::make_shared<MemorySource>(false);
    base->add("Models/a.glb", "base a");
    base->add("Models/b.glb", "base b");
    base->add("readme.txt", "base readme");
    auto patch = std::make_shared<MemorySource>(false);
    patch->add("Models/b.glb", "patched b");
    patch->add("Models/c.glb", "patch c");

    VirtualFileSystem fileSystem;
    REQUIRE(fileSystem.mount("assets", base) != 0);
    REQUIRE(fileSystem.mount("assets", patch) != 0);

    CHECK(text(fileSystem.read("assets://Models/a.glb")) == "base a");
    CHECK(text(fileSystem.read("assets://Models/b.glb")) == "patched b");
    CHECK(text(fileSystem.read("assets://Models/c.glb")) == "patch c");
    CHECK_FALSE(fileSystem.read("assets://Models/missing.glb"));
    CHECK_FALSE(fileSystem.read("other://Models/a.glb"));

    CHECK(names(fileSystem.list("assets://")) == std::vector<std::string>{"Models", "readme.txt"});
    CHECK(names(fileSystem.list("assets://Models")) == std::vector<std::string>{"a.glb", "b.glb", "c.glb"});
    CHECK(fileSystem.isDirectory("assets://Models"));
    CHECK(fileSystem.isFile("assets://readme.txt"));
    CHECK(fileSystem.info("assets://Models/b.glb").size == std::string_view("patched b").size());
    CHECK(fileSystem.sourceOf("assets://Models/a.glb") == "memory");

    SUBCASE("a folder in an upper layer hides a file of the same name")
    {
        patch->add("readme.txt/inside.txt", "x");
        CHECK(fileSystem.isDirectory("assets://readme.txt"));
        CHECK_FALSE(fileSystem.read("assets://readme.txt"));
    }

    SUBCASE("unmounting a layer reveals the one below")
    {
        const std::vector<MountInfo> mounts = fileSystem.mounts();
        REQUIRE(mounts.size() == 2);
        CHECK(fileSystem.unmount(mounts.front().id));
        CHECK(text(fileSystem.read("assets://Models/b.glb")) == "base b");
        CHECK_FALSE(fileSystem.exists("assets://Models/c.glb"));
        CHECK_FALSE(fileSystem.unmount(mounts.front().id));
    }
}

TEST_CASE("Mount names are validated")
{
    VirtualFileSystem fileSystem;
    CHECK(fileSystem.mount("assets", std::make_shared<MemorySource>()) != 0);
    CHECK(fileSystem.mount("my-mod_2", std::make_shared<MemorySource>()) != 0);
    CHECK(fileSystem.mount("A", std::make_shared<MemorySource>()) == 0);
    CHECK(fileSystem.mount("c", std::make_shared<MemorySource>()) == 0);
    CHECK(fileSystem.mount("Assets", std::make_shared<MemorySource>()) == 0);
    CHECK(fileSystem.mount("my mod", std::make_shared<MemorySource>()) == 0);
    CHECK(fileSystem.mount("assets", nullptr) == 0);
}

TEST_CASE("Writes go to the first writable layer")
{
    auto readOnly = std::make_shared<MemorySource>(false);
    readOnly->add("settings.ini", "default");
    auto writable = std::make_shared<MemorySource>(true);
    auto top = std::make_shared<MemorySource>(false);

    VirtualFileSystem fileSystem;
    fileSystem.mount("user", readOnly);
    fileSystem.mount("user", writable);
    fileSystem.mount("user", top);

    CHECK(fileSystem.writeText("user://settings.ini", "changed"));
    CHECK(text(fileSystem.read("user://settings.ini")) == "changed");
    CHECK(text(readOnly->read("settings.ini")) == "default");
    CHECK(text(writable->read("settings.ini")) == "changed");

    CHECK(fileSystem.writeText("user://Layouts/Wide.ini", "wide"));
    CHECK(fileSystem.isDirectory("user://Layouts"));
    CHECK_FALSE(fileSystem.writeText("user://", "root"));

    CHECK(fileSystem.remove("user://settings.ini"));
    CHECK(text(fileSystem.read("user://settings.ini")) == "default");
    CHECK_FALSE(fileSystem.remove("user://settings.ini"));
    CHECK_FALSE(fileSystem.remove("user://Layouts"));

    VirtualFileSystem readOnlyOnly;
    readOnlyOnly.mount("assets", readOnly);
    CHECK_FALSE(readOnlyOnly.writeText("assets://new.txt", "x"));
    CHECK_FALSE(readOnlyOnly.createDirectory("assets://New"));
}

TEST_CASE("A directory source reads and writes a native folder")
{
    const TemporaryFolder folder;
    VirtualFileSystem fileSystem;
    fileSystem.mount("data", std::make_shared<DirectorySource>(folder.utf8(), true));

    // Names outside ASCII, which need wide-character APIs on Windows.
    const std::string name = "data://Sk\xC3\xBD/\xE6\x97\xA5\xE6\x9C\xAC.txt";
    REQUIRE(fileSystem.writeText(name, "first"));
    REQUIRE(fileSystem.writeText(name, "second, longer"));
    CHECK(text(fileSystem.read(name)) == "second, longer");
    CHECK(fileSystem.info(name).size == std::string_view("second, longer").size());
    CHECK(fileSystem.info(name).modifiedTime != 0);

    // The temporary file a write goes through is renamed away.
    CHECK(names(fileSystem.list("data://Sk\xC3\xBD")) == std::vector<std::string>{"\xE6\x97\xA5\xE6\x9C\xAC.txt"});

    CHECK(fileSystem.createDirectory("data://A/B/C"));
    CHECK(fileSystem.isDirectory("data://A/B/C"));
    CHECK(names(fileSystem.list("data://")) == std::vector<std::string>{"A", "Sk\xC3\xBD"});

    const std::string native = fileSystem.nativePath(name);
    CHECK(native.starts_with(folder.utf8()));
    CHECK(fileSystem.virtualPath(native) == name);
    CHECK(fileSystem.virtualPath(folder.utf8()) == "data://");
    CHECK(fileSystem.virtualPath(folder.utf8() + "-sibling/x.txt").empty());
    CHECK(fileSystem.nativePath("data://Missing/new.txt") == folder.utf8() + "/Missing/new.txt");

    CHECK_FALSE(fileSystem.writeText("data://../escaped.txt", "x"));
    CHECK_FALSE(std::filesystem::exists(folder.path.parent_path() / "escaped.txt"));

    CHECK(fileSystem.remove(name));
    CHECK_FALSE(fileSystem.exists(name));
    CHECK(fileSystem.remove("data://A/B/C"));
    CHECK_FALSE(fileSystem.remove("data://A"));
}

TEST_CASE("readFile() resolves virtual paths through the active file system")
{
    auto source = std::make_shared<MemorySource>(false);
    source->add("Shaders/Mesh.vs.dxil", "bytecode");
    VirtualFileSystem fileSystem;
    fileSystem.mount("engine", source);
    const ActiveFileSystem active(fileSystem);

    CHECK(text(readFile("engine://Shaders/Mesh.vs.dxil")) == "bytecode");
    CHECK(fileExists("engine://Shaders/Mesh.vs.dxil"));
    CHECK_FALSE(fileExists("engine://Shaders"));
    CHECK(fileSize("engine://Shaders/Mesh.vs.dxil") == 8u);
    CHECK_FALSE(readFile("engine://Shaders/Missing.dxil"));

    const TemporaryFolder folder;
    const std::string native = folder.utf8() + "/native.txt";
    {
        VirtualFileSystem scratch;
        scratch.mount("scratch", std::make_shared<DirectorySource>(folder.utf8(), true));
        REQUIRE(scratch.writeText("scratch://native.txt", "on disk"));
    }
    CHECK(text(readFile(native)) == "on disk");
    CHECK(fileSize(native) == 7u);
}

TEST_CASE("The mount table can change while other threads read")
{
    auto source = std::make_shared<MemorySource>(false);
    source->add("a.txt", "a");
    VirtualFileSystem fileSystem;
    fileSystem.mount("assets", source);

    std::atomic<bool> stop{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i)
        readers.emplace_back([&] {
            while (!stop)
                if (text(fileSystem.read("assets://a.txt")) != "a")
                    ++failures;
        });
    for (int i = 0; i < 2000; ++i)
    {
        const MountId id = fileSystem.mount("assets", std::make_shared<MemorySource>(false));
        fileSystem.unmount(id);
    }
    stop = true;
    for (std::thread& reader : readers)
        reader.join();
    CHECK(failures == 0);
}
