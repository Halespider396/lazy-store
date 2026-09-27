#ifndef FUZZY_H
#define FUZZY_H

/* Returns -1 if `pattern` is not a subsequence of `str` (case-insensitive).
 * Otherwise returns a non-negative score where higher = better match
 * (consecutive runs, word-boundary starts, and matches near the front
 * of the string are rewarded; gaps between matched chars are penalized). */
int fuzzy_score(const char *pattern, const char *str);

#endif
