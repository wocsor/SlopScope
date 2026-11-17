#include "string.h"
void *memcpy(void *destination, const void *source, size_t byte_count) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    while (byte_count--) {
        *destination_bytes++ = *source_bytes++;
    }
    return destination;
}
void *memmove(void *destination, const void *source, size_t byte_count) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    if (destination_bytes < source_bytes) {
        memcpy(destination, source, byte_count);
    } else {
        while (byte_count) {
            byte_count--;
            destination_bytes[byte_count] = source_bytes[byte_count];
        }
    }
    return destination;
}
void *memset(void *destination, int fill_value, size_t byte_count) {
    unsigned char *destination_bytes = destination;
    while (byte_count--) {
        *destination_bytes++ = (unsigned char)fill_value;
    }
    return destination;
}
int memcmp(const void *left, const void *right, size_t byte_count) {
    const unsigned char *left_bytes = left, *right_bytes = right;
    while (byte_count--) {
        int difference = *left_bytes++ - *right_bytes++;
        if (difference) {
            return difference;
        }
    }
    return 0;
}
size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}
char *strcpy(char *destination, const char *source) {
    char *destination_cursor = destination;
    while ((*destination_cursor++ = *source++)) {
    }
    return destination;
}
char *strchr(const char *text, int character) {
    do {
        if (*text == (char)character) {
            return (char *)text;
        }
    } while (*text++);
    return 0;
}
size_t strspn(const char *text, const char *set) {
    size_t length = 0;
    while (text[length] && strchr(set, text[length])) {
        length++;
    }
    return length;
}
size_t strcspn(const char *text, const char *set) {
    size_t length = 0;
    while (text[length] && !strchr(set, text[length])) {
        length++;
    }
    return length;
}
