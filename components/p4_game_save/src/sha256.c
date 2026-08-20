// SPDX-License-Identifier: MIT

#include "sha256.h"

#include <string.h>

static const uint32_t s_round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
    UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
    UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
    UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
    UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
    UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
    UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
    UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
    UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
    UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
    UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
    UINT32_C(0xc67178f2),
};

static uint32_t rotate_right(uint32_t value, unsigned amount)
{
    return (value >> amount) | (value << (32U - amount));
}

static uint32_t read_u32_be(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24U) |
        ((uint32_t)bytes[1] << 16U) |
        ((uint32_t)bytes[2] << 8U) |
        (uint32_t)bytes[3];
}

static void write_u32_be(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void transform(p4_game_save_sha256_t *context,
                      const uint8_t block[64])
{
    uint32_t words[64];
    for (size_t index = 0U; index < 16U; ++index) {
        words[index] = read_u32_be(&block[index * 4U]);
    }
    for (size_t index = 16U; index < 64U; ++index) {
        const uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
            rotate_right(words[index - 15U], 18U) ^
            (words[index - 15U] >> 3U);
        const uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
            rotate_right(words[index - 2U], 19U) ^
            (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 +
            words[index - 7U] + s1;
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    for (size_t index = 0U; index < 64U; ++index) {
        const uint32_t sum1 = rotate_right(e, 6U) ^
            rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + sum1 + choose +
            s_round_constants[index] + words[index];
        const uint32_t sum0 = rotate_right(a, 2U) ^
            rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void p4_game_save_sha256_init(p4_game_save_sha256_t *context)
{
    if (context == NULL) {
        return;
    }
    *context = (p4_game_save_sha256_t){
        .state = {
            UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
            UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
            UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
            UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
        },
    };
}

void p4_game_save_sha256_update(p4_game_save_sha256_t *context,
                                const uint8_t *bytes,
                                size_t length)
{
    if (context == NULL || (bytes == NULL && length != 0U)) {
        return;
    }
    context->byte_count += (uint64_t)length;
    while (length != 0U) {
        size_t take = sizeof(context->block) - context->block_used;
        if (take > length) {
            take = length;
        }
        memcpy(&context->block[context->block_used], bytes, take);
        context->block_used += take;
        bytes += take;
        length -= take;
        if (context->block_used == sizeof(context->block)) {
            transform(context, context->block);
            context->block_used = 0U;
        }
    }
}

void p4_game_save_sha256_finish(p4_game_save_sha256_t *context,
                                uint8_t digest[32])
{
    if (context == NULL || digest == NULL) {
        return;
    }
    const uint64_t bit_count = context->byte_count * UINT64_C(8);
    const uint8_t one = UINT8_C(0x80);
    p4_game_save_sha256_update(context, &one, 1U);
    const uint8_t zero = 0U;
    while (context->block_used != 56U) {
        p4_game_save_sha256_update(context, &zero, 1U);
    }
    uint8_t length_bytes[8];
    for (size_t index = 0U; index < sizeof(length_bytes); ++index) {
        length_bytes[7U - index] =
            (uint8_t)(bit_count >> (index * 8U));
    }
    p4_game_save_sha256_update(context, length_bytes, sizeof(length_bytes));
    for (size_t index = 0U; index < 8U; ++index) {
        write_u32_be(&digest[index * 4U], context->state[index]);
    }
    memset(context, 0, sizeof(*context));
}
