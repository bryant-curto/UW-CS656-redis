#include <stdio.h>

#define oprintf(...) { printf(">> "__VA_ARGS__); fflush(stdout); }
//#define oprintf(...) { }
#define eprintf(...) { fprintf(stderr, ">> ERROR: "__VA_ARGS__); fflush(stderr); exit(-1); }
