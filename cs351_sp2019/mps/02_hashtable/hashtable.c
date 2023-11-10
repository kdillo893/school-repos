#include "hashtable.h"
#include <stdlib.h>
#include <string.h>

/* Daniel J. Bernstein's "times 33" string hash function, from comp.lang.C;
   See https://groups.google.com/forum/#!topic/comp.lang.c/lSKWXiuNOAk */
unsigned long hash(char *str) {

  unsigned long hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

hashtable_t *make_hashtable(unsigned long size) {

  hashtable_t *ht = malloc(sizeof(hashtable_t));
  ht->size = size;
  ht->buckets = calloc(sizeof(bucket_t *), size);
  return ht;
}

void ht_put(hashtable_t *ht, char *key, void *val) {

  // hash to bucket sizes, check the bucket for key match
  unsigned int idx = hash(key) % ht->size;
  bucket_t *b = ht->buckets[idx];
  while (b) {
    if (strcmp(b->key, key) == 0) {
      // overwrite the val for the bucket on match and return 
      // free to open up space, that was used, then replace it. (next is primitive addr)
      free(b->val);
      free(b->key);
      b->key = key;
      b->val = val;

      return;
    }

    // no match, go next in LList
    b = b->next;
  }

  // didn't return, create new and add to list.
  b = malloc(sizeof(bucket_t));
  b->key = key;
  b->val = val;

  // creating one points to old next (prepend LList)
  b->next = ht->buckets[idx];
  ht->buckets[idx] = b;
}

void *ht_get(hashtable_t *ht, char *key) {
  unsigned int idx = hash(key) % ht->size;
  bucket_t *b = ht->buckets[idx];
  while (b) {
    if (strcmp(b->key, key) == 0) {
      return b->val;
    }
    b = b->next;
  }
  return NULL;
}

void ht_iter(hashtable_t *ht, int (*f)(char *, void *)) {

  //does the order of iteration matter
  bucket_t *b;
  unsigned long i;
  for (i = 0; i < ht->size; i++) {
    b = ht->buckets[i];
    while (b) {
      if (!f(b->key, b->val)) {
        return; // abort iteration
      }
      b = b->next;
    }
  }
}

void ht_del(hashtable_t *ht, char *key) {
  //complexity O(1) + O(b)... only bad if unbalanced hash or low buckets
  
  unsigned int idx = hash(key) % ht->size;
  bucket_t *b = ht->buckets[idx];
  bucket_t *priorb = NULL;

  while (b) {

    if (strcmp(b->key, key) == 0) {
      if (priorb == NULL) {
        ht->buckets[idx] = b->next;
      } else {
        priorb->next = b->next;
      }

      free_bucket(b);
      return;
    }

    //not found, go next
    priorb = b;
    b = b->next;
  }
}

void ht_rehash(hashtable_t *ht, unsigned long newsize) {
  //currently this is using O(n) space, O(n) time to scale all

  // new buckets array of new size within ht.
  bucket_t **newbuckets = calloc(newsize, sizeof(bucket_t *));

  for (int i = 0; i < ht->size; i++) {
    bucket_t *b = ht->buckets[i];
    while (b) {
      // 1. evaluate the bucket's key hash with new size
      unsigned int nidx = hash(b->key) % newsize;

      // 2. save the "next bucket" for iteration purposes
      bucket_t * nextb = b->next;

      // 3. place "old bucket" in "new buckets" with prepend (no freeing b/c data maintained)
      b->next = newbuckets[nidx];
      newbuckets[nidx] = b;

      // 4. put the "next bucket" from current hash table as "b"
      b = nextb;
    }
  }

  // fix pointer of newbuckets to ht->buckets after freeing it...
  free(ht->buckets);
  ht->buckets = newbuckets;
  ht->size = newsize;
}

void free_bucket(bucket_t *b) {
  // remove key/val ptr ref, then b itself...
  free(b->key);
  free(b->val);
  // b.next is a pointer to the next bucket, which may still be in use.
  free(b);
}

void free_hashtable(hashtable_t *ht) {
  // free each bucket and its contents; loop over size worht buckets
  for (int i = 0; i < ht->size; i++) {
    if (ht->buckets[i] != NULL) {
      bucket_t *b = ht->buckets[i];
      bucket_t *b_next;
      while (b != NULL) {
        // scale the linked list, free prior ones.
        b_next = b->next;
        free_bucket(b);
        b = b_next;
      }
      // null out the bucket ref... not sure if needed but we'll do it.
      ht->buckets[i] = NULL;
    }
  }

  // free the memory of the allocated bucket space, then ht
  free(ht->buckets);
  free(ht);
}
