#include <ctype.h>
#include <string.h>
#include "fuzzy.h"

#define CONSECUTIVE_BONUS 15
#define WORD_BOUNDARY_BONUS 10
#define FIRST_CHAR_BONUS 12
#define GAP_PENALTY 2

static int is_word_boundary(char prev) {
    return prev == '\0' || prev == '-' || prev == '_' || prev == ' ' || prev == '/';
}

int fuzzy_score(const char *pattern, const char *str) {
    if (!pattern || !*pattern) return 0; /* empty pattern matches everything */
    if (!str) return -1;

    int score = 0;
    int consecutive = 0;
    size_t str_pos = 0;
    size_t str_len = strlen(str);

    for (size_t p = 0; pattern[p] != '\0'; p++) {
        char pc = (char)tolower((unsigned char)pattern[p]);
        int found = -1;

        for (size_t s = str_pos; s < str_len; s++) {
            char sc = (char)tolower((unsigned char)str[s]);
            if (sc == pc) {
                found = (int)s;
                break;
            }
        }

        if (found < 0) return -1; /* pattern char not found: not a subsequence */

        size_t gap = (size_t)found - str_pos;
        if (found > 0 && (size_t)found == str_pos && consecutive > 0) {
            /* handled below via str_pos tracking */
        }

        if (str_pos != 0 && (size_t)found == str_pos) {
            consecutive++;
            score += CONSECUTIVE_BONUS + consecutive;
        } else {
            consecutive = 0;
            score -= (int)gap * GAP_PENALTY;
        }

        if (found == 0) {
            score += FIRST_CHAR_BONUS;
        } else if (is_word_boundary(str[found - 1])) {
            score += WORD_BOUNDARY_BONUS;
        }

        str_pos = (size_t)found + 1;
    }

    /* Slight preference for shorter overall strings (tighter match) */
    score -= (int)(str_len / 8);

    return score;
}
