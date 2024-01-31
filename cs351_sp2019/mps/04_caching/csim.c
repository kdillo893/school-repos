#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachelab.h"

// have int vars declared for the cache counting
//(fuck passing by reference from main)
int hit_count, miss_count, eviction_count;

void load_trace(char *tracefile) {

  char buf[1000];
  uint64_t addr = 0;
  unsigned int len = 0;
  FILE *fp = fopen(tracefile, "r");

  if (!fp) {
    fprintf(stderr, "%s: %s\n", tracefile, strerror(errno));
    exit(1);
  }

  // trace loads 1000ch per line at a time.
  while (fgets(buf, 1000, fp) != NULL) {
    if (buf[1] == 'S' || buf[1] == 'L' || buf[1] == 'M') {
      sscanf(buf + 3, "%lx,%u", &addr, &len);

      // at this point, we know the "address", "action" and "how many things"

      printf("%c, %lx\n", buf[1], addr);
    }
  }

  fclose(fp);
}

void print_usage(char *argv[]) {
  printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
  printf("Options:\n");
  printf("  -h         Print this help message.\n");
  printf("  -v         Optional verbose flag.\n");
  printf("  -s <num>   Number of set index bits.\n");
  printf("  -E <num>   Number of lines per set.\n");
  printf("  -b <num>   Number of block offset bits.\n");
  printf("  -t <file>  Trace file.\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  char c;
  int verbose = 0;
  int s, S, E, b, B;
  char *trace_file;

  while ((c = getopt(argc, argv, "vs:E:b:t:")) != -1) {
    // printing the opt to see if I'm crazy:

    char *theArg = optarg;
    printf("optarg=%s, opt=%d\n", theArg, c);

    switch (c) {
    case 'v':
      verbose = 1;
      break;
    case 's':
      s = atoi(optarg);
      break;
    case 'E':
      E = atoi(optarg);
      break;
    case 'b':
      b = atoi(optarg);
      break;
    case 't':
      trace_file = optarg;
      break;
    default:
      print_usage(argv);
      exit(1);
    }
  }

  // printf("Using verbose mode? %s\n", verbose? "yes" : "no");
  printf("v=%d, s=%d, E=%d, b=%d, trace_file=%s\n", verbose, s, E, b,
         trace_file);

  if (s == 0 || E == 0 || b == 0 || trace_file == NULL) {
    printf("%s: Missing required command line argument\n", argv[0]);
    print_usage(argv);
    exit(1);
  }

  S = (unsigned int)pow(2, s);
  B = (unsigned int)pow(2, b);

  if (verbose) {
    printf("Using cache with S=%d, E=%d, B=%d\n", S, E, B);
  }

  hit_count = 0, miss_count = 0, eviction_count = 0;

  load_trace(trace_file);

  printSummary(hit_count, miss_count, eviction_count);
  return 0;
}
