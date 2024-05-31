#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void hello_rs(void);

/**
 * Return true if the argv contains any element equal to `needle`.
 *
 * # Safety
 * argv must be a null-terminated array of C strings; needle must be a valid C string.
 */
int argv_contains(char **argv, const char *needle);
