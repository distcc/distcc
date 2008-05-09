#ifndef STRINGMAP_H
#define STRINGMAP_H

typedef struct {
	/* the strings, and what they map to */
	struct {
		char *key;
		char *value;
	} *map;

	/* number of elements in map */
	int n;

	/* if nonzero, ignore all but this many trailing words,
         * where words are separated by the '/' char
	 * Example:
	 * 	comparison	num=1	num=2	num=3	
	 *	a/b/z =? 1/y/z	match	no	no
	 *	a/b/z =? 1/b/z	match	match	no
	 */
	int numFinalWordsToMatch;
} stringmap_t;

stringmap_t *stringmap_load(const char *filename, int numFinalWordsToMatch);
const char *stringmap_lookup(const stringmap_t *map, const char *string);

#endif
