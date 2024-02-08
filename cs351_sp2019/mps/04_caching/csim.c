#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachelab.h"

/**
 * the block contains valid, tag, data, ... something else?
 */
typedef struct cache_block {
  _Bool valid;
  _Bool dirty; // has the cache been modified?

  unsigned int lru_track; // tracing "when this was used", can replace lowest.
  uint64_t tag;

  // data would be here if truly making a cache.

} cache_block_t;

typedef struct cache {
  unsigned int E;         // Associativity, depth of lines.
  unsigned int S;         // sets, indexes of lines
  unsigned int B;         // block size, bytes per "segment of line"
  unsigned int s;         // set addr bits
  unsigned int b;         // block addr bits
  unsigned int e;         // offset of block width, from E associativity.
  unsigned int tag_width; // bits for the tag, taking a subset of the 64bit.

  // any other supporting things?
  cache_block_t **blocks;

} cache_t;

typedef struct perf_tracker {
  unsigned int hit_count;
  unsigned int miss_count;
  unsigned int eviction_count;

  unsigned int access_count;
} perf_t;

// what functions do I need to operate on the "cache" that I make?

void load_trace_with_cache(char *tracefile, cache_t **myCache, perf_t **performance, _Bool verbose) {

  char buf[1000];
  uint64_t addr = 0;
  unsigned int len = 0;
  FILE *fp = fopen(tracefile, "r");

  if (!fp) {
    fprintf(stderr, "%s: %s\n", tracefile, strerror(errno));
    exit(1);
  }

  char *lineRead = fgets(buf, 1000, fp);
  // trace loads 1000ch per line at a time.
  while (1) {
    if (lineRead == NULL) {
      break;
    }

    printf("%s", lineRead);

    char theOp = buf[1];

    if (theOp == 'S' || theOp == 'L' || theOp == 'M') {
      sscanf(buf + 3, "%lx,%u", &addr, &len);

      // at this point, we know the "address", "action" and "how many things"
      if (verbose) {
        printf("%c, %lx\n", theOp, addr);
      }



    }

    // next line
    lineRead = fgets(buf, 1000, fp);
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
  _Bool verbose = 0;
  int s, S, E, b, B;
  char *trace_file;

  // getopt does parsing for values if "[char]:", no value if "[char]"
  while ((c = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
    switch (c) {
    case 'h':
      print_usage(argv);
      return 0;
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

  if (s <= 0 || E <= 0 || b <= 0 || trace_file == NULL) {
    printf("%s: Missing required command line argument\n", argv[0]);
    print_usage(argv);
    exit(1);
  }

  S = (unsigned int)pow(2, s);
  B = (unsigned int)pow(2, b);
  // get associativity bits, effectively a "log2"
  unsigned int e = 0;
  for (int test = E; test > 0; e++) {
    test /= 2;
  }

  if (verbose) {
    printf("Using myCache with S=%d, E=%d, B=%d, on tracefile=%s\n", S, E, B,
           trace_file);
  }

  // establish the myCache memory object using the above values.
  //  B = block size (2^b); bytes allocated in data. b=1 means 2 byte blocks.
  //      b=5 means 32 byte blocks
  //  S = number of myCache indexes (2^s): s=10 means 2^10 (1024) myCache
  //  indexes,
  //      from 00,0000,0000 to 11,1111,1111
  //  E = Associativity (# of lines per set)...
  //
  // effective "myCache size" is S lines with E blocks of B size.
  // each myCache line would need a way of tracking which one was accessed most
  // recently.

  unsigned int blockCount = S * E * B;
  cache_block_t* blocks = malloc(blockCount * sizeof(cache_block_t));

  //put the blocks into a larger object.
  cache_t* myCache = malloc(sizeof(cache_t));
  myCache->blocks = &blocks;
  myCache->E = E;
  myCache->B = B;
  myCache->S = S;
  myCache->e = e;
  myCache->b = b;
  myCache->s = s;
  myCache->tag_width = 64 - e - b - s;

  perf_t *performance = malloc(sizeof(perf_t));
  performance->hit_count = 0;
  performance->miss_count = 0;
  performance->eviction_count = 0;
  performance->access_count = 0;

  load_trace_with_cache(trace_file, &myCache, &performance, verbose);

  free(blocks);
  free(myCache);

  printSummary(performance->hit_count, performance->miss_count,
               performance->eviction_count);

  free(performance);


  return 0;
}
