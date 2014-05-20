#ifndef DEBUG_H
#define DEBUG_H

extern int debug;

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define _debug(FMT, ...)						\
	do {								\
		if (debug)						\
			printf("Debug:  "FMT, ##__VA_ARGS__);		\
	} while (0)

#define _error(FMT, ...)						\
	do {								\
		fprintf(stderr, "\nERROR at line %d of %s() in \"%s\":\n\t" \
				FMT"\n", __LINE__, __func__, __FILE__,	\
							##__VA_ARGS__);	\
	} while (0)

#define _enter(FMT, ...) _debug(">>>>>  %s() "FMT"\n", __func__, ##__VA_ARGS__)
#define _leave(FMT, ...) _debug("<<<<<  %s() "FMT"\n", __func__, ##__VA_ARGS__)

#endif
