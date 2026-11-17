/*
 * LZSS codec matching the HANMATEK DOS1102 boot1 decompressor (F1C200s).
 *
 * Format, reverse-engineered from the stock image at flash 0x349000:
 *   - 4096-byte ring window, zero-filled, write pointer starting at 4080.
 *   - Flag byte per 8 items, LSB first: 1 = literal byte, 0 = match.
 *   - Match = 2 bytes: b0 = pos & 0xFF, b1 = ((pos >> 4) & 0xF0) | (len - 3),
 *     where pos is an ABSOLUTE ring index and len is 3..18.
 *
 * usage: lzss d <in> <out> <usize>     decompress (stops after usize bytes)
 *        lzss c <in> <out>             compress
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_BYTES 4096
#define MAX_MATCH_BYTES 18
#define MIN_MATCH_BYTES 3
#define INITIAL_WINDOW_POSITION (WINDOW_BYTES - 16)
#define MATCH_HASH_BITS 15
#define MAX_MATCH_CANDIDATES 512

static uint8_t *read_file_bytes(const char *path, size_t *byte_count) {
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        perror(path);
        exit(1);
    }
    fseek(stream, 0, SEEK_END);
    *byte_count = (size_t)ftell(stream);
    fseek(stream, 0, SEEK_SET);
    uint8_t *data = malloc(*byte_count ? *byte_count : 1);
    if (fread(data, 1, *byte_count, stream) != *byte_count) {
        perror("read");
        exit(1);
    }
    fclose(stream);
    return data;
}

static void write_file_bytes(const char *path, const uint8_t *data, size_t byte_count) {
    FILE *stream = fopen(path, "wb");
    if (!stream || fwrite(data, 1, byte_count, stream) != byte_count) {
        perror(path);
        exit(1);
    }
    fclose(stream);
}

static size_t decompress(const uint8_t *compressed_data,
                         size_t compressed_bytes,
                         uint8_t *output,
                         size_t output_limit) {
    uint8_t window[WINDOW_BYTES] = {0};
    unsigned window_position = INITIAL_WINDOW_POSITION, token_flags = 0;
    size_t input_position = 0, output_position = 0;
    while (input_position < compressed_bytes && output_position < output_limit) {
        token_flags >>= 1;
        if (!(token_flags & 0x100)) {
            token_flags = compressed_data[input_position++] | 0xFF00;
            if (input_position >= compressed_bytes) {
                break;
            }
        }
        if (token_flags & 1) {
            uint8_t decoded_byte = compressed_data[input_position++];
            output[output_position++] = decoded_byte;
            window[window_position] = decoded_byte;
            window_position = (window_position + 1) & (WINDOW_BYTES - 1);
        } else {
            if (input_position + 1 >= compressed_bytes) {
                break;
            }
            unsigned match_low_byte = compressed_data[input_position],
                     match_high_byte = compressed_data[input_position + 1];
            input_position += 2;
            unsigned match_offset = match_low_byte | ((match_high_byte & 0xF0) << 4),
                     match_bytes = (match_high_byte & 0x0F) + MIN_MATCH_BYTES;
            for (unsigned match_index = 0;
                 match_index < match_bytes && output_position < output_limit;
                 match_index++) {
                uint8_t decoded_byte = window[(match_offset + match_index) & (WINDOW_BYTES - 1)];
                output[output_position++] = decoded_byte;
                window[window_position] = decoded_byte;
                window_position = (window_position + 1) & (WINDOW_BYTES - 1);
            }
        }
    }
    return output_position;
}

/* Limit match distance so the decoder retains source bytes until copying completes. */
static size_t compress(const uint8_t *input, size_t input_bytes, uint8_t *output) {
    int32_t *hash_heads = malloc(sizeof(int32_t) << MATCH_HASH_BITS);
    int32_t *previous_matches = malloc(sizeof(int32_t) * (input_bytes ? input_bytes : 1));
    for (size_t bucket_index = 0; bucket_index < ((size_t)1 << MATCH_HASH_BITS); bucket_index++) {
        hash_heads[bucket_index] = -1;
    }
    size_t output_position = 0, flag_position = 0, input_position = 0;
    unsigned flag_bit = 8;
#define MATCH_HASH(position)                                                                       \
    ((((uint32_t)input[position] << 10) ^ ((uint32_t)input[position + 1] << 5) ^                   \
      input[position + 2]) &                                                                       \
     ((1u << MATCH_HASH_BITS) - 1))
#define INSERT_MATCH_POSITION(position)                                                            \
    do {                                                                                           \
        if ((position) + 2 < input_bytes) {                                                        \
            uint32_t hash_bucket = MATCH_HASH(position);                                           \
            previous_matches[position] = hash_heads[hash_bucket];                                  \
            hash_heads[hash_bucket] = (int32_t)(position);                                         \
        }                                                                                          \
    } while (0)
    while (input_position < input_bytes) {
        if (flag_bit == 8) {
            flag_position = output_position++;
            output[flag_position] = 0;
            flag_bit = 0;
        }
        size_t best_match_bytes = 0, best_match_position = 0;
        if (input_position + MIN_MATCH_BYTES <= input_bytes) {
            int32_t candidate_position = hash_heads[MATCH_HASH(input_position)];
            int candidates_checked = 0;
            size_t maximum_match_bytes = input_bytes - input_position < MAX_MATCH_BYTES
                                             ? input_bytes - input_position
                                             : MAX_MATCH_BYTES;
            while (candidate_position >= 0 && candidates_checked++ < MAX_MATCH_CANDIDATES &&
                   input_position - (size_t)candidate_position <= WINDOW_BYTES - MAX_MATCH_BYTES) {
                size_t match_bytes = 0;
                while (match_bytes < maximum_match_bytes &&
                       input[candidate_position + match_bytes] ==
                           input[input_position + match_bytes]) {
                    match_bytes++;
                }
                if (match_bytes > best_match_bytes) {
                    best_match_bytes = match_bytes;
                    best_match_position = (size_t)candidate_position;
                    if (match_bytes == maximum_match_bytes) {
                        break;
                    }
                }
                candidate_position = previous_matches[candidate_position];
            }
        }
        if (best_match_bytes >= MIN_MATCH_BYTES) {
            unsigned window_position =
                (unsigned)((INITIAL_WINDOW_POSITION + best_match_position) & (WINDOW_BYTES - 1));
            output[output_position++] = (uint8_t)(window_position & 0xFF);
            output[output_position++] =
                (uint8_t)(((window_position >> 4) & 0xF0) | (best_match_bytes - MIN_MATCH_BYTES));
            for (size_t match_index = 0; match_index < best_match_bytes; match_index++) {
                INSERT_MATCH_POSITION(input_position + match_index);
            }
            input_position += best_match_bytes;
        } else {
            output[flag_position] |= (uint8_t)(1u << flag_bit);
            output[output_position++] = input[input_position];
            INSERT_MATCH_POSITION(input_position);
            input_position++;
        }
        flag_bit++;
    }
    free(hash_heads);
    free(previous_matches);
    return output_position;
}

int main(int argc, char **argv) {
    if (argc >= 5 && argv[1][0] == 'd') {
        size_t compressed_bytes, expected_bytes = strtoul(argv[4], NULL, 0);
        uint8_t *compressed_data = read_file_bytes(argv[2], &compressed_bytes),
                *output = malloc(expected_bytes);
        size_t output_bytes = decompress(compressed_data, compressed_bytes, output, expected_bytes);
        write_file_bytes(argv[3], output, output_bytes);
        printf("decompressed %zu bytes (asked %zu)\n", output_bytes, expected_bytes);
        return output_bytes == expected_bytes ? 0 : 2;
    }
    if (argc >= 4 && argv[1][0] == 'c') {
        size_t input_bytes;
        uint8_t *input = read_file_bytes(argv[2], &input_bytes),
                *output = malloc(input_bytes + input_bytes / 8 + 16);
        size_t output_bytes = compress(input, input_bytes, output);
        write_file_bytes(argv[3], output, output_bytes);
        printf("compressed %zu -> %zu bytes\n", input_bytes, output_bytes);
        return 0;
    }
    fprintf(stderr, "usage: %s d <in> <out> <usize> | c <in> <out>\n", argv[0]);
    return 1;
}
