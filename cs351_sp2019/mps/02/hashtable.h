#ifndef HASHTABLE_T
#define HASHTABLE_T

typedef struct hashtable hashtable_t;
typedef struct bucket bucket_t;

/** Linked list with key/value pair */
struct bucket {
  char *key;
  void *val;
  bucket_t *next;
};

struct hashtable {
  unsigned long size;
  bucket_t **buckets;
};

unsigned long hash(char *str);

/** Initialize hashtable with a number of buckets. Put for a given k,v pair 
    will place in bucket linked list with [hash() % size] index. */
hashtable_t *make_hashtable(unsigned long size);

/** Put the value in the hashtable with the given key.*/
void  ht_put(hashtable_t *ht, char *key, void *val);

/** Retrieve the value for a given key.*/
void *ht_get(hashtable_t *ht, char *key);
/** Delete the key/value pair in the hashtable, freeing memory.*/
void  ht_del(hashtable_t *ht, char *key);
/** Iterate through all buckets with information, unsorted, performing a given function (f).
    If the function (f) returns a falsey value, break iteration. */
void  ht_iter(hashtable_t *ht, int (*f)(char *, void *));
/** Re-package hashtable with new amount of buckets.*/
void  ht_rehash(hashtable_t *ht, unsigned long newsize);
/** Free memory usage from all buckets, then free bucket array pointer and hashtable.*/
void  free_hashtable(hashtable_t *ht);
/** Free memory from an individual bucket (which contains a key/value pair).*/
void  free_bucket(bucket_t *b);

#endif
