
#include <string_view>
#include <string>
//#include <array>
#include <format>
#include <cstdint>
#include <random>

#include "gtest/gtest.h"

#include "gzipranges.hpp"
#include <zlib.h>

using namespace std;
using namespace  jgaa::ranges::zlib;

namespace {

} // anon ns

TEST(gzipranges, SmallCompress) {
    constexpr string_view input = "teste";

    std::string compressed;

    auto range = gz_compressor<decltype(input)>{input};

    std::ranges::copy(range, std::back_inserter(compressed));

    // int uncompress(Bytef * dest, uLongf * destLen, const Bytef * source, uLong sourceLen);

    string uncompressed;
    uncompressed.resize(input.size());

    uLongf dest_size = uncompressed.size();

    //// Don't work with gzip. Works fine with plain inflate.
    // auto res = uncompress(
    //     reinterpret_cast<uint8_t *>(uncompressed.data()),
    //     &dest_size,
    //     reinterpret_cast<const uint8_t *>(compressed.data()),
    //     compressed.size());

    // EXPECT_NE(res, Z_BUF_ERROR);
    // EXPECT_NE(res, Z_MEM_ERROR);
    // EXPECT_NE(res, Z_DATA_ERROR);

    gz_uncompress_all(compressed, uncompressed);

    EXPECT_EQ(input, uncompressed);
}

TEST(gzipranges, LargerCompress) {

    constexpr size_t insize = 1024 * 1024;
    std::vector<char> input(insize);

    // Generate random data to make it harder to compress. That should make sure
    // we iterate both input and output buffers in the compressor.
    ranges::generate(input, []() {
        static std::random_device rd;
        static std::uniform_int_distribution<uint32_t> dist(0,0xff);
        return static_cast<char>(dist(rd));
    });

    std::string compressed;

    // With random data, the "compressed" buffer is likely to be larger than the input.
    compressed.reserve(input.size() + 1024);

    auto range = gz_compressor<decltype(input)>{input};

    std::ranges::copy(range, std::back_inserter(compressed));

    std::clog << "Compressed " << input.size() << " bytes to " << compressed.size() << " bytes." << endl;

    // int uncompress(Bytef * dest, uLongf * destLen, const Bytef * source, uLong sourceLen);

    string uncompressed;
    uncompressed.resize(input.size());

    string_view input_view{input.data(), input.size()};

    gz_uncompress_all(compressed, uncompressed);
    EXPECT_EQ(input_view, uncompressed);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
