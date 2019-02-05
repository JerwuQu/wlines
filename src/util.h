#define E(b)                                                                       \
    if (b)                                                                         \
        do {                                                                       \
            fprintf(stderr, "Fatal error on line %d! Out of memory?\n", __LINE__); \
            exit(1);                                                               \
    } while (0)

#define WE(b)                                                         \
    if (!(b))                                                         \
        do {                                                          \
            fprintf(stderr, "Windows error on line %d!\n", __LINE__); \
            exit(1);                                                  \
    } while (0)

#ifdef DEBUG
#define _IF_DBG(body) \
    do {              \
        body;         \
    } while (0)
#else
#define _IF_DBG(body)
#endif
