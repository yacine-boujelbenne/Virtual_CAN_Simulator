#include <gtest/gtest.h>
#include "Sha256.hpp"
#include <string>
using std::string;

TEST(Sha256, KnownVectors) {
    EXPECT_EQ(sha256Hex(""),"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc"),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256Hex("The quick brown fox jumps over the lazy dog"),
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(Sha256, LongInputAndBlockBoundaries) {
    EXPECT_EQ(sha256Hex(string(1000000,'a')),
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // 55, 56 and 64 bytes exercise every padding branch around the block size.
    EXPECT_EQ(sha256Hex(string(55,'x')).size(),64u);
    EXPECT_NE(sha256Hex(string(55,'x')),sha256Hex(string(56,'x')));
    EXPECT_NE(sha256Hex(string(63,'x')),sha256Hex(string(64,'x')));
    EXPECT_NE(sha256Hex(string(64,'x')),sha256Hex(string(65,'x')));
}

TEST(Sha256, IsStableAndSensitive) {
    const string payload="scenario+definitions+can_map+run";
    EXPECT_EQ(sha256Hex(payload),sha256Hex(payload));
    EXPECT_NE(sha256Hex(payload),sha256Hex(payload+" "));
    EXPECT_EQ(sha256Hex(payload).size(),64u);
}
