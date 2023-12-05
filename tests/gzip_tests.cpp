
#include <string_view>
#include <string>
#include <array>
#include <format>
#include <cstdint>

#include "gtest/gtest.h"

#include "gzipranges.hpp"
#include <zlib.h>

using namespace std;

namespace {

template <input_buffer_range_of_bytes In, output_buffer_range_of_bytes Out>
void gzipUncompress(In& in, Out& out) {

    z_stream strm{};

    const auto wsize = MAX_WBITS | 16;

    if (inflateInit2(&strm, wsize) != Z_OK) {
        throw runtime_error{"Failed to initialize decompression"};
    }

    strm.avail_in = in.size();
    strm.next_in = reinterpret_cast<uint8_t *>(in.data());

    strm.avail_out = out.size();
    strm.next_out = reinterpret_cast<uint8_t *>(out.data());

    const auto result = inflate(&strm, Z_SYNC_FLUSH);
    if (result != Z_STREAM_END) {
        throw runtime_error{format("Failed to decompress. Error {}", result)};
    }
}

} // anon ns

TEST(gzipranges, SmallCompress) {
    constexpr string_view input = "teste";

    std::string compressed;

    auto range = ZipRangeProcessor<decltype(input)>{input};

    std::ranges::copy(range, std::back_inserter(compressed));

    // int uncompress(Bytef * dest, uLongf * destLen, const Bytef * source, uLong sourceLen);

    string uncompressed;
    uncompressed.resize(input.size());

    uLongf dest_size = uncompressed.size();

    // auto res = uncompress(
    //     reinterpret_cast<uint8_t *>(uncompressed.data()),
    //     &dest_size,
    //     reinterpret_cast<const uint8_t *>(compressed.data()),
    //     compressed.size());

    // EXPECT_NE(res, Z_BUF_ERROR);
    // EXPECT_NE(res, Z_MEM_ERROR);
    // EXPECT_NE(res, Z_DATA_ERROR);

    gzipUncompress(compressed, uncompressed);

    EXPECT_EQ(input, uncompressed);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
