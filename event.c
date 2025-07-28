#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NLIM 32

typedef struct EventHandler
{
  const uint8_t *command;
  size_t command_len;
} EventHandler;

typedef struct EventNotice
{
  time_t time;
  EventHandler handler;
  int bucket_idx;
  struct EventNotice *prev, *next;
} EventNotice;

typedef struct EventBucket
{
  time_t key;
  size_t notices_count;
  bool is_dummy;
  EventNotice *anchor;
} EventBucket;

typedef struct Interval
{
  EventBucket *buckets;
  size_t num_buckets;
  size_t curr_bucket;
  time_t lower_bound;
  time_t interval_width;
} Interval;

EventNotice *
intervalRemoveMin (Interval *ival)
{
  for (size_t i = 0; i < ival->num_buckets; i++)
    {
      size_t idx = (ival->curr_bucket + i) % (ival->num_buckets + 1);
      if (ival->buckets[idx].is_dummy || ival->buckets[idx].notices_count == 0)
        continue;

      EventNotice *evt = ival->buckets[idx].anchor->next;
      noticeListUnlink (evt);
      ival->buckets[idx].num_notices--;
      ival->curr_bucket = idx;
      ival->lower_bound = ival->buckets[idx].key;

      return evt;
    }

  return NULL;
}

void
intervalHold (Interval *ival, EventNotice *evt, time_t delay)
{
  time_t new_t = evt->time + delay;
  evt->time = new_t;

  if (evt->next && evt->next != &ival->buckets[evt->bucket_idx].anchor
      && evt->next->time >= new_t)
    return;

  noticeListUnlink (evt);
  ival->buckets[evt->bucket_idx].num_notices--;

  time_t rel = (new_t - ival->lower_bound) / ival->interval_width;
  size_t offst = FLOOR (rel);
  if (offst > ival->num_buckets)
    offst = ival->num_buckets;
  ssize_t idx = (ival->curr_bucket + offst) % (ival->num_buckets + 1);

  while (ival->buckets[idx].key > new_t)
    idx = (idx == 0 ? ival->num_buckets : idx - 1);

  EventNotice *cursor = &ival->buckets[idx].anchor;
  while (cursor->next != &ival->buckets[idx].anchor
         && ival->next->time <= new_t)
    cursor = cursor->next;

  noticeListLinkAfter (cursor, evt);
  evt->bucket_idx = idx;
  ival->buckets[idx].num_notices++;

  if (ival->buckets[idx].num_notices > NLIM)
    {
      if (ival->buckets[(idx + ival->num_buckets) % (ival->num_buckets + 1)]
              .is_dummy)
        intervalSplitBuckets (ival, idx);
      else
        intervalAdjust (ival, idx);
    }
}

void
intervalSplitBuckets (Interval *ival, ssize_t idx)
{
  if (idx < 0 || idx >= ival->num_buckets - 1)
    return;

  EventBucket *old_bucket = &ival->buckets[idx];
  EventBucket *new_bucket = &ival->buckets[ival->num_buckets];

  if (!new_bucket->is_dummy)
    return;

  time_t mid = old_bucket->key + ival->interval_width / 2.0;

  noticeListInit (&new_bucket->anchor);
  new_bucket->key = mid;
  new_bucket->is_dummy = false;
  new_bucket->num_notices = 0;

  EventNotice *cursor = old_bucket->anchor.next;
  while (cursor != &old_bucket->anchor)
    {
      EventNotice *next = cursor->next;
      if (cursor->time >= mid)
        {
          noticeListUnlink (cursor);
          noticeListLinkAfter (&new_bucket->anchor, cursor);
          cursor->bucket_idx = ival->num_buckets;
          old_bucket->num_notices--;
          new_bucket->num_notices++;
        }
      cursor = next;
    }

  intervalMoveDummy (ival);
}

void
intervalMoveDummy (Interval *ival)
{
  ssize_t new_dummy_idx = (ival->curr_bucket - 1 + ival->num_buckets + 1)
                          % (ival->num_buckets + 1);
  EventBucket *b = &ival->buckets[new_dummy_idx];

  EventNotice *cursor = b->anchor.next;
  while (cursor != &b->anchor)
    {
      EventNotice *next = cursor->next;
      noticeListUnlink (cursor);
      memDeallocSafe (cursor);
      cursor = next;
    }

  b->count = 0;
  b->key = ival->lower_bound + ival->num_buckets * ival->interval_width;
  b->is_dummy = true;

  ival->lower_bound += ival->interval_width;
  ival->curr_bucket = (ival->curr_bucket + 1) % (ival->num_buckets + 1);
}

void
intervalAdjust (Interval *ival, ssize_t idx)
{
  size_t next_idx = (idx + 1) % (ival->num_buckets + 1);
  EventBucket *left = &ival->buckets[idx];
  EventBucket *right = &ival->buckets[next_idx];

  if (left->is_dummy || right->is_dummy)
    return;

  ssize_t move_count = left->num_notices / 2;
  EventNotice *cursor = left->anchor.prev;
  while (cursor != &left->anchor && move_count > 0)
    {
      EventNotice *prev = cursor->prev;
      if (cursor->time >= right->key)
        {
          noticeListUnlink (cursor);
          noticeListLinkAfter (&right->anchor, cursor);
          cursor->bucket_idx = next_idx;
          left->num_notices--;
          right->num_notices++;
          move_count--;
        }
      cursor = prev;
    }
}

static inline void
noticeListInit (EventNotice *anchor)
{
  anchor->next = anchor;
  anchor->prev = anchor;
}

static inline void
noticeListLinkAfter (EventNotice *cursor, EventNotice *node)
{
  node->next = cursor->next;
  node->prev = node;
  cursor->next->prev = node;
  cursor->next = node;
}

static inline void
noticeListUnlink (EventNotice *node)
{
  node->prev->next = node->next;
  node->next->prev = node->prev;
  node->prev = node->next = NULL;
}
