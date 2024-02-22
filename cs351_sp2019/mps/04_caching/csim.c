#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachelab.h"

// I made this too complicated. I could just simulate in one main with an
// initialized 2d array...

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
  unsigned int associativity;      // Associativity, depth of lines.
  unsigned int sets_size;          // sets, indexes of lines
  unsigned int block_size;         // block size, bytes per "segment of line"
  unsigned int set_bits;           // set addr bits
  unsigned int block_bits;         // block addr bits
  unsigned int associativity_bits; // offset of block width, from associativity
                                   // associativity.
  unsigned int tag_width; // bits for the tag, taking a subset of the 64bit.

  // blocks will be indexed by set and associativity like [i][j]...
  cache_block_t **blocks;

} cache_t;

typedef struct perf_tracker {
  unsigned int hit_count;
  unsigned int miss_count;
  unsigned int eviction_count;

  unsigned int access_count;
} perf_t;

typedef struct hit_info {
  _Bool valid_hit;
  uint64_t set;
  uint64_t block;
  uint64_t e_line;
} hit_info_t;

cache_t *create_cache_struct(const unsigned int associativity,
                             const unsigned int set_bits,
                             const unsigned int block_bits) {

  cache_t *theCache = malloc(sizeof(cache_t));
  theCache->set_bits = set_bits;
  theCache->block_bits = block_bits;
  theCache->associativity = associativity;

  // calculate the sets, blocks, and associativity bits given the provided
  // values
  theCache->sets_size = (unsigned int)pow(2, set_bits);
  theCache->block_size = (unsigned int)pow(2, block_bits);
  // get associativity bits, effectively a "log2"
  unsigned int associativity_bits = 0;
  for (int test = associativity; test > 1; associativity_bits++) {
    test /= 2;
  }
  theCache->associativity_bits = associativity_bits;
  theCache->tag_width = 64 - associativity_bits - block_bits - set_bits;

  // assigning pointer to pointers of cache blocks... the pointer is "a set",
  // the internal would be "the associative part of the set"
  theCache->blocks = malloc(theCache->sets_size * sizeof(cache_block_t *));

  // block bit is column, associativity bit is row... within sets.
  // malloc doesn't make all the blocks "0'd" guarantee, so we do that.
  for (int i = 0; i < theCache->sets_size; i++) {

    // this set, assign a count of blocks equal to associativity.
    cache_block_t *blocks_in_set =
        malloc(theCache->associativity * sizeof(cache_block_t));
    
    for (int j = 0; j < associativity; j++) {
      blocks_in_set[j].dirty = 0;
      blocks_in_set[j].valid = 0;
      blocks_in_set[j].tag = 0;
    }

    theCache->blocks[i] = blocks_in_set;

  }

  return theCache;
}

void free_cache(cache_t *cache) {
  // might need to loop for blocks, but it's currently calloc'd.
  free(cache->blocks);
  free(cache);
}

// tag bits are "the rest of the address offset that can't see the block"
uint64_t parse_tag_from_addr(cache_t *cache, uint64_t addr) {
  uint64_t tag_shift =
      cache->set_bits + cache->block_bits + cache->associativity_bits;
  uint64_t tag_mask = -1 >> tag_shift;
  uint64_t tag_bits = tag_mask & (addr >> tag_shift);

  return tag_bits;
}

/**
 * cache has the tag width, use that and mask the addr to compare block tag.
 * */
hit_info_t search_hit_index(cache_t *cache, uint64_t addr, _Bool verbose) {

  uint64_t tag = parse_tag_from_addr(cache, addr);
  // using the set bits from addr to locate line in cache.
  uint64_t set_mask = cache->sets_size - 1;
  uint64_t set =
      set_mask & (addr >> (cache->block_bits + cache->associativity_bits));

  uint64_t assoc_mask = cache->associativity - 1;
  uint64_t assoc = assoc_mask & (addr >> cache->block_bits);

  // use the addr to determine tag, set#, and block offset.
  uint64_t block_mask = cache->block_size - 1;
  uint64_t block = block_mask & addr;

  if (verbose) {
    fprintf(stdout,
            "tag=0x%X, block=0x%x, set=0x%X, addr=0x%X, cachestats={s=%d, "
            "b=%d, E=%d, S=%d, B=%d, "
            "e=%d}\n",
            (unsigned int)tag, (unsigned int)block, (unsigned int)set,
            (unsigned int)addr, cache->set_bits, cache->block_bits,
            cache->associativity, cache->sets_size, cache->block_size,
            cache->associativity_bits);
  }
  // have block, set, and tag,
  // looping over all the sets, you go by "blocks size*i*associativity"

  // for the given set, check the tag match.


  cache_block_t* theSet = cache->blocks[set];
  cache_block_t theBlock = theSet[assoc];

  hit_info_t theHit;
  theHit.valid_hit = 0;
  if (tag == theBlock.tag && theBlock.valid) {
    // hit, return the hit info.
    theHit.valid_hit = 1;
    theHit.set = set;
    theHit.block = block;
    theHit.e_line = assoc;
  }
  return theHit;
}

// if miss, find open spot and put thing in there.
int perform_miss(cache_t *cache, perf_t *performance, uint64_t addr,
                 char theOp) {
  // add to miss count
  performance->miss_count++;

  // get the tag which will place on the set line.
  uint64_t tag = parse_tag_from_addr(cache, addr);

  // loop over the cache sets, do something with it when found..
  int lowest_LRU_idx = -1;
  int lowest_LRU = (1 << sizeof(int)) - 1;
  int found_empty = 0;
  unsigned int idx_to_replace = 0;
  for (int i = 0; i < cache->sets_size; i++) {
    unsigned int set_idx = i;

    cache_block_t* theSet = cache->blocks[set_idx];
    for (int j = 0; j < cache->associativity; j++) {

      cache_block_t theBlock = theSet[j];

      // checking by valid bits.
      if (theBlock.valid == 0) {
        // this is our guy to use.
        idx_to_replace = set_idx;
        found_empty = 1;
        break;
      }

      // tracking the LRU.
      if (lowest_LRU > theBlock.lru_track) {
        lowest_LRU = theBlock.lru_track;
        lowest_LRU_idx = set_idx;
      }
    }

    if (found_empty) {
      break;
    }
  }

  // if nothing empty found, we are evicting.
  if (!found_empty) {
    idx_to_replace = lowest_LRU_idx;
    performance->eviction_count++;
  }

  cache_block_t * theSet = cache->blocks[idx_to_replace];
  for (int j = 0; j < cache->associativity; j++) {
    cache_block_t theBlock = theSet[j];

    theBlock.lru_track = performance->access_count;
    theBlock.valid = 1;

    if (theOp == 'M' || theOp == 'S') {
      theBlock.dirty = 1;
    } else if (theOp == 'L') {
      theBlock.dirty = 0;
    }
    theBlock.tag = tag;

    performance->access_count++;

    if (theOp == 'M') {
      // we just replaced things, call hit on the store.
      performance->hit_count++;
    }

    cache->blocks[idx_to_replace][j] = theBlock;
  }

  return idx_to_replace;
}

void print_usage(char *argv[]) {
  printf("Usage: %s [-hv] -s <num> -associativity <num> -b <num> -t <file>\n",
         argv[0]);
  printf("Options:\n");
  printf("  -h         Print this help message.\n");
  printf("  -v         Optional verbose flag.\n");
  printf("  -s <num>   Number of set index bits.\n");
  printf("  -associativity <num>   Number of lines per set.\n");
  printf("  -b <num>   Number of block offset bits.\n");
  printf("  -t <file>  Trace file.\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  char c;
  _Bool verbose = 0;
  int set_bits, associativity, block_bits;
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
      set_bits = atoi(optarg);
      break;
    case 'E':
      associativity = atoi(optarg);
      break;
    case 'b':
      block_bits = atoi(optarg);
      break;
    case 't':
      trace_file = optarg;
      break;
    default:
      print_usage(argv);
      exit(1);
    }
  }

  if (set_bits <= 0 || associativity <= 0 || block_bits <= 0 ||
      trace_file == NULL) {
    printf("%s: Missing required command line argument\n", argv[0]);
    print_usage(argv);
    exit(1);
  }

  // put the blocks into a larger object.
  cache_t *myCache = create_cache_struct(associativity, set_bits, block_bits);

  if (myCache == NULL) {
    printf("Error creating cache");
    return 1;
  }

  if (verbose) {
    printf("Using myCache with S=%d, associativity=%d, B=%d, on trace_file=%s\n",
           myCache->sets_size, associativity, myCache->block_size, trace_file);
  }

  perf_t *performance = malloc(sizeof(perf_t));
  performance->hit_count = 0;
  performance->miss_count = 0;
  performance->eviction_count = 0;
  performance->access_count = 0;

  //Start parsing the trace file
  char buf[1000];
  uint64_t addr = 0;
  unsigned int len = 0;
  FILE *fp = fopen(trace_file, "r");

  if (!fp) {
    fprintf(stderr, "%s: %s\n", trace_file, strerror(errno));
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

      // cache operation. S = store, L = Load, M = Modify (Load then store)
      // check for hit, if hit then increment.
      hit_info_t hit_check = search_hit_index(myCache, addr, verbose);
      printf("hit?=%d, set=%ld, e=%ld, block=%ld\n", hit_check.valid_hit,
             hit_check.set, hit_check.e_line, hit_check.block);

      if (hit_check.valid_hit) {
        // increment for hit
        performance->hit_count++;
        uint64_t set = hit_check.set;
        uint64_t e_line = hit_check.e_line;

        // block to touch is dependent on set and associativity.
        cache_block_t* theSet = myCache->blocks[set];
        cache_block_t theBlock = theSet[e_line];

        // if store or modify, dirty. if loading, clean.
        if (theOp == 'S' || theOp == 'M') {
          theBlock.dirty = 1;
        } else if (theOp == 'L') {
          theBlock.dirty = 0;
        }

        // modify gives extra count, b/c load and store.
        if (theOp == 'M') {
          performance->hit_count++;
        }

        // update the access count, and set the line's LRU value.
        theBlock.lru_track = performance->access_count;
        performance->access_count++;

        //set the block back after modifying.
        myCache->blocks[set][e_line] = theBlock;
      } else {
        // this is miss, perform miss operation.
        // miss would look for open space, and if not evict the LRU.
        perform_miss(myCache, performance, addr, theOp);
      }
    }

    // next line
    lineRead = fgets(buf, 1000, fp);
  }

  //finish parsing the trace file, close and show results.
  fclose(fp);
  free_cache(myCache);

  printSummary(performance->hit_count, performance->miss_count,
               performance->eviction_count);

  free(performance);

  return 0;
}
