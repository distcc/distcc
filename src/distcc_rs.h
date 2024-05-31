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

/**
 * Free an arg list whose entries are themselves allocated on the C heap.
 *
 * # Safety
 *
 * `argv` must point to a malloced array of pointers to malloced strings, terminated by a null.
 */
void dcc_free_argv(char **argv);

/**
 * Return the length of an argv list, not counting the null terminator.
 *
 * # Safety
 * `argv` must point to a null-terminated array of pointers.
 */
int dcc_argv_len(char **argv);
