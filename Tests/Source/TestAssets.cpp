#include "TestAssets.h"

#include "Core/BinaryStream.h"

namespace arc::test
{
    namespace
    {
        std::string base64(std::span<const std::byte> data)
        {
            constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string text;
            for (size_t i = 0; i < data.size(); i += 3)
            {
                uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;
                if (i + 1 < data.size())
                    chunk |= static_cast<uint32_t>(data[i + 1]) << 8;
                if (i + 2 < data.size())
                    chunk |= static_cast<uint32_t>(data[i + 2]);
                text += kAlphabet[(chunk >> 18) & 63];
                text += kAlphabet[(chunk >> 12) & 63];
                text += i + 1 < data.size() ? kAlphabet[(chunk >> 6) & 63] : '=';
                text += i + 2 < data.size() ? kAlphabet[chunk & 63] : '=';
            }
            return text;
        }
    }

    std::vector<std::byte> makeBmp(uint32_t width, uint32_t height, uint8_t red)
    {
        BinaryWriter writer;
        const uint32_t pixelBytes = width * height * 4;
        writer.write(uint16_t{0x4D42});
        writer.write(uint32_t{54 + pixelBytes});
        writer.write(uint32_t{0});
        writer.write(uint32_t{54});
        writer.write(uint32_t{40});
        writer.write(static_cast<int32_t>(width));
        writer.write(static_cast<int32_t>(height));
        writer.write(uint16_t{1});
        writer.write(uint16_t{32});
        writer.write(uint32_t{0});
        writer.write(pixelBytes);
        writer.write(int32_t{2835});
        writer.write(int32_t{2835});
        writer.write(uint32_t{0});
        writer.write(uint32_t{0});
        for (uint32_t i = 0; i < width * height; ++i)
        {
            const uint8_t bgra[4] = {static_cast<uint8_t>(i), 64, red, 255};
            writer.write(bgra);
        }
        return writer.take();
    }

    std::string triangleGltf()
    {
        const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const std::string buffer = base64(std::as_bytes(std::span(positions)));
        return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Tri","mesh":0,"translation":[1,2,3]}],)"
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
               R"("bufferViews":[{"buffer":0,"byteLength":36}],)"
               R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" +
               buffer + R"("}]})";
    }
}
