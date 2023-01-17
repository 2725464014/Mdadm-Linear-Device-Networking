#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

int cached = 0;
int inserted = 0;
int cache_create(int num_entries)
{
  if (num_entries >= 2 && num_entries <= 4096)
  {

    if (cached == 0)
    {
      cache = calloc(num_entries, sizeof(cache_entry_t)); // dynamically allocate space for cache
      cache_size = num_entries;
      cached = 1;
      return 1;
    }
  }
  return -1;
}

int cache_destroy(void)
{
  if (cached == 1)
  {
    free(cache);
    cache = NULL;
    cache_size = 0;
    cached = 0;
    inserted = 0;
    return 1;
  }
  return -1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf)
{
  if (inserted == 0)
  { // checks if anything has been inserted
    return -1;
  }
  num_queries++;
  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && buf != NULL)
    {
      num_hits++;
      cache[i].num_accesses += i;
      memcpy(buf, cache[i].block, 256);
      return 1;
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf)
{
  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      memcpy(cache[i].block, buf, 256);
      cache[i].num_accesses += 1;
    }
  }
}
int cache_insert(int disk_num, int block_num, const uint8_t *buf)
{
  int position = -1;
  int lfu;
  // check invalid parameters
  if (cached == 0 || buf == NULL || cache_size == 0 || disk_num > 16 || disk_num < 0 || block_num > 256 || block_num < 0)
  {
    return -1;
  }

  inserted = 1;
  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && block_num != 0 && disk_num != 0)
    { // inserting existing entry fails; also checks zeros because elements in calloc are 0
      return -1;
    }
    if (cache[i].valid == 0)
    {
      position = i;
      break;
    }
  }
  // cache is full
  if (position == -1)
  {
    lfu = cache[0].num_accesses;
    position = 0;
    for (int i = 1; i < cache_size; i++)
    {
      if (cache[i].num_accesses < lfu)
      {
        lfu = cache[i].num_accesses;
        position = i;
      }
    }
  }
  memcpy(cache[position].block, buf, 256);
  cache[position].disk_num = disk_num;
  cache[position].block_num = block_num;
  cache[position].valid = 1; // block has valid data in it
  cache[position].num_accesses = 1;
  return 1;
}
bool cache_enabled(void)
{
  return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void)
{
  fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float)num_hits / num_queries);
}