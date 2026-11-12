/**
 * @file fuzz_driver.c
 * @brief Minimal stand-in for libFuzzer when the compiler is not Clang (for example local GCC builds).
 *
 * Usage: fuzz_<target> [-max_total_time=SECONDS] [-seed=N] <corpus dir or file>...
 * Runs every seed once, then mutates them (bit flips, byte overwrites, inserts, deletes, splices) until the time is
 * up. Not coverage-guided: CI uses real libFuzzer with Clang; this finds shallow bugs and lets the targets run
 * anywhere. Any sanitizer report or trap fails the run (the input is saved to crash-<n>.bin first).
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/** Largest input generated. */
#define FUZZ_MAX_INPUT 4096
/** Seeds kept in memory. */
#define MAX_SEEDS 256

static uint8_t *s_seed[MAX_SEEDS];
static size_t s_seed_len[MAX_SEEDS];
static size_t s_nseeds;
static uint64_t s_rng = 88172645463325252ULL;

/** @brief xorshift64 random numbers (deterministic for a given -seed). */
static uint64_t rnd(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return s_rng;
}

/** @brief Load one file as a seed. */
static void add_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL || s_nseeds >= MAX_SEEDS) {
        if (f != NULL) {
            fclose(f);
        }
        return;
    }
    uint8_t *buf = malloc(FUZZ_MAX_INPUT);
    const size_t n = buf != NULL ? fread(buf, 1, FUZZ_MAX_INPUT, f) : 0;
    fclose(f);
    s_seed[s_nseeds] = buf;
    s_seed_len[s_nseeds++] = n;
}

/** @brief Load a directory of seeds, or a single file. */
static void add_path(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) {
        add_file(path);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
            add_file(full);
        }
    }
    closedir(d);
}

/** @brief Run one input; save it first so a crash can be reproduced. */
static void run(const uint8_t *data, size_t len, const char *crash_path) {
    FILE *f = fopen(crash_path, "wb");
    if (f != NULL) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
    LLVMFuzzerTestOneInput(data, len);
}

int main(int argc, char **argv) {
    unsigned seconds = 10;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-max_total_time=", 16) == 0) {
            seconds = (unsigned)atoi(argv[i] + 16);
        } else if (strncmp(argv[i], "-seed=", 6) == 0) {
            s_rng ^= (uint64_t)atoll(argv[i] + 6) * 0x9E3779B97F4A7C15ULL;
        } else if (argv[i][0] != '-') {
            add_path(argv[i]);
        }
    }
    const char *crash = "crash-last-input.bin";
    for (size_t i = 0; i < s_nseeds; i++) {
        run(s_seed[i], s_seed_len[i], crash);
    }
    uint8_t cur[FUZZ_MAX_INPUT];
    const time_t end = time(NULL) + seconds;
    uint64_t runs = 0;
    while (time(NULL) < end) {
        for (int batch = 0; batch < 2000; batch++) {
            size_t len = 0;
            if (s_nseeds > 0 && rnd() % 8 != 0) {
                const size_t k = rnd() % s_nseeds;
                len = s_seed_len[k];
                memcpy(cur, s_seed[k], len);
            } else {
                len = rnd() % 64;
                for (size_t j = 0; j < len; j++) {
                    cur[j] = (uint8_t)rnd();
                }
            }
            const unsigned muts = 1 + (unsigned)(rnd() % 6);
            for (unsigned m = 0; m < muts; m++) {
                switch (rnd() % 6) {
                case 0:
                    if (len > 0) {
                        cur[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8));
                    }
                    break;
                case 1:
                    if (len > 0) {
                        cur[rnd() % len] = (uint8_t)rnd();
                    }
                    break;
                case 2:
                    if (len + 1 < FUZZ_MAX_INPUT) {
                        const size_t p = len > 0 ? rnd() % len : 0;
                        memmove(cur + p + 1, cur + p, len - p);
                        cur[p] = (uint8_t)rnd();
                        len++;
                    }
                    break;
                case 3:
                    if (len > 1) {
                        const size_t p = rnd() % len;
                        memmove(cur + p, cur + p + 1, len - p - 1);
                        len--;
                    }
                    break;
                case 4:
                    if (len > 0) { /* Interesting values: boundaries that trigger integer bugs. */
                        static const uint8_t V[] = {0x00, 0x01, 0x7F, 0x80, 0xFF, 0xFE, 0x0F, 0x10};
                        cur[rnd() % len] = V[rnd() % sizeof(V)];
                    }
                    break;
                default:
                    if (s_nseeds > 0 && len > 0) { /* Splice a piece of another seed. */
                        const size_t k = rnd() % s_nseeds;
                        if (s_seed_len[k] > 0) {
                            const size_t from = rnd() % s_seed_len[k];
                            size_t n = 1 + rnd() % 16;
                            n = n > s_seed_len[k] - from ? s_seed_len[k] - from : n;
                            const size_t to = rnd() % len;
                            n = n > len - to ? len - to : n;
                            memcpy(cur + to, s_seed[k] + from, n);
                        }
                    }
                    break;
                }
            }
            run(cur, len, crash);
            runs++;
        }
    }
    remove(crash);
    printf("fuzz driver: %llu inputs, no failure\n", (unsigned long long)runs);
    return 0;
}
