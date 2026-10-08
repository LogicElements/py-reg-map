/* Minimal assertion helpers for the host C tests. */
#ifndef CHECK_H_
#define CHECK_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int check_failures;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond))                                                          \
    {                                                                     \
      check_failures++;                                                   \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                     \
  } while (0)

/* Compare `len` bytes at `actual` with the listed bytes (length must match too). */
#define CHECK_BYTES(actual, len, ...)                                     \
  do {                                                                    \
    const uint8_t expected_[] = {__VA_ARGS__};                            \
    CHECK((size_t)(len) == sizeof(expected_)                              \
          && memcmp((actual), expected_, sizeof(expected_)) == 0);        \
  } while (0)

#endif /* CHECK_H_ */
