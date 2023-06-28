#include <stdlib.h>
#include <string.h>
#include "hashtable.h"

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
  
  //to check existing, scale the bucket until "key" reached
  unsigned int idx = hash(key) % ht->size;
  bucket_t *b = ht->buckets[idx];
  while(b){
    //check for key match, if so, set val and return
    if (strcmp(b->key, key) == 0){
      //free pointers to val/key; then re-assign new ones 
      //(for some reason overwrite failed)
      free(b->val); 
      free(b->key);
      b->key = key;
      b->val = val;
      return;
    }
    b = b->next;
  }
  
  //didn't return, so create new and add to list.
  b = malloc(sizeof(bucket_t));
  b->key = key;
  b->val = val;
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
  bucket_t *b;
  unsigned long i;
  for (i=0; i<ht->size; i++) {
    b = ht->buckets[i];
    while (b) {
      if (!f(b->key, b->val)) {
        return ; // abort iteration
      }
      b = b->next;
    }
  }
}

void  ht_del(hashtable_t *ht, char *key) {
  //lookup the key, if exists, free it and update links.
  unsigned int idx = hash(key) % ht->size;
  bucket_t *b = ht->buckets[idx];
  bucket_t *priorb = NULL;
  while(b) {
    if (strcmp(b->key, key) == 0) {
      //key match to this bucket, remove reference and free.
      bucket_t *nextb = b->next;
      if (priorb == NULL){
        ht->buckets[idx] = nextb;
      } else {
        priorb->next = nextb;
      }
      //remove all pointers, then free bucket...
      b->next = NULL;
      free_bucket(b);
      return;
    }
    priorb = b;
    b = b->next;
  }
}

void  ht_rehash(hashtable_t *ht, unsigned long newsize) {
    //buckets == all the old stuff...
    //different buckets array == new size with bucket allocs...
    //loop buckets, for each b, eval key hash to new bucket,
    //  ... point to that b in new buckets array... continue.
    //after all b's are pointed to in new,....
    //realloc buckets to the new size, newbuckets pointer on that...
    
    //new buckets array of new size within ht.
    bucket_t** newbuckets = calloc(newsize, sizeof(bucket_t *));
    
    //iter over current set of linkedlist buckets,
    //move bucket objects and replace next link.
    for(int i=0; i < ht->size; i++){
        bucket_t *b = ht->buckets[i];
        while(b){
            //for each b in this linkedlist,
            //1. evaluate key hash with new size
            //2. place in bucket similar to put, remove old next
            //-- no need to keymatch, since all keys are diff.
            unsigned int nidx = hash(b->key) % newsize;
            bucket_t *bnext = b->next;
            b->next = newbuckets[nidx];
            newbuckets[nidx]=b;
            b = bnext;
        }
    }

    //fix pointer of newbuckets to ht->buckets after freeing it...
    free(ht->buckets);
    ht->buckets = newbuckets;
    ht->size = newsize;
}

void free_bucket(bucket_t *b) {
  //remove key/val ptr ref, then b itself...
  free(b->key);
  free(b->val);
  //b.next is a pointer to the next bucket, which may still be in use.
  free(b);
}

void free_hashtable(hashtable_t *ht) {
  //free each bucket and its contents; loop over size worht buckets
  for (int i = 0; i < ht->size; i++){
    if (ht->buckets[i] != NULL) {
      bucket_t * b = ht->buckets[i];
      bucket_t * b_next;
      while(b != NULL){
        //scale the linked list, free prior ones.
        b_next = b->next;
        free_bucket(b);
        b = b_next;
      }
      //null out the bucket ref... not sure if needed but we'll do it.
      ht->buckets[i] = NULL;
    }
  }
  
  //free the memory of the allocated bucket space, then ht
  free(ht->buckets);
  free(ht);
}