#define POSIX_SOURCE
#define POSIX_C_SOURCE
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "lykron.h"

Scheduler *
schedulerNew (time_t interval_width, size_t num_buckets)
{
  Scheduler *sched = memAllocSafe (sizeof (Scheduler));
  sched->buckets = memAllocBlockSafe (num_buckets + 1, sizeof (EventBucket));
  sched->num_buckets = num_buckets;
  sched->curr_bucket = 0;
  sched->lower_bound = 0;
  sched->interval_width = interval_width;

  for (size_t i = 0; i < sched->num_buckets + 1; i++)
    {
      sched->buckets[i].key
          = (i < sched->num_buckets
                 ? sched->lower_bound + i * sched->interval_width
                 : TIME_UNSPEC);
      sched->buckets[i].num_notices = 0;
      sched->buckets[i].is_dummy = (i == 0 || i == sched->num_bucekts);
      noticeListInit (&sched->buckets[i].anchor);
    }
}

void
schedulerDelete (Scheduler *sched)
{
  for (size_t i = 0; i < sched->num_buckets + 1; i++)
    {
      EventNotice *evt = &sched->buckets[i].anchor;
      while (evt != NULL)
        {
          EventNotice *next = evt->next;
          memDeallocSafe (evt);
          evt = next;
        }
    }

  memDeallocSafe (sched->buckets);
  memDeallocSafe (sched);
}

EventNotice *
schedulerRemoveMin (Scheduler *sched)
{
  for (size_t i = 0; i < sched->num_buckets; i++)
    {
      size_t idx = (sched->curr_bucket + i) % (sched->num_buckets + 1);
      if (sched->buckets[idx].is_dummy || sched->buckets[idx].num_notices == 0)
        continue;

      EventNotice *evt = sched->buckets[idx].anchor->next;
      noticeListUnlink (evt);
      sched->buckets[idx].num_notices--;
      sched->curr_bucket = idx;
      sched->lower_bound = sched->buckets[idx].key;

      return evt;
    }

  return NULL;
}

void
schedulerHold (Scheduler *sched, EventNotice *evt, time_t delay)
{
  time_t new_t = evt->time + delay;
  evt->time = new_t;

  if (evt->next && evt->next != &sched->buckets[evt->bucket_idx].anchor
      && evt->next->time >= new_t)
    return;

  noticeListUnlink (evt);
  sched->buckets[evt->bucket_idx].num_notices--;

  time_t rel = (new_t - sched->lower_bound) / sched->interval_width;
  size_t offst = FLOOR (rel);
  if (offst > sched->num_buckets)
    offst = sched->num_buckets;
  ssize_t idx = (sched->curr_bucket + offst) % (sched->num_buckets + 1);

  while (sched->buckets[idx].key > new_t)
    idx = (idx == 0 ? sched->num_buckets : idx - 1);

  EventNotice *cursor = &sched->buckets[idx].anchor;
  while (cursor->next != &sched->buckets[idx].anchor
         && sched->next->time <= new_t)
    cursor = cursor->next;

  noticeListLinkAfter (cursor, evt);
  evt->bucket_idx = idx;
  sched->buckets[idx].num_notices++;

  if (sched->buckets[idx].num_notices > NLIM)
    {
      if (sched->buckets[(idx + sched->num_buckets) % (sched->num_buckets + 1)]
              .is_dummy)
        schedulerSplit (sched, idx);
      else
        schedulerAdjust (sched, idx);
    }
}

void
schedulerSplit (Scheduler *sched, ssize_t idx)
{
  if (idx < 0 || idx >= sched->num_buckets - 1)
    return;

  EventBucket *old_bucket = &sched->buckets[idx];
  EventBucket *new_bucket = &sched->buckets[sched->num_buckets];

  if (!new_bucket->is_dummy)
    return;

  time_t mid = old_bucket->key + sched->interval_width / 2.0;

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
          cursor->bucket_idx = sched->num_buckets;
          old_bucket->num_notices--;
          new_bucket->num_notices++;
        }
      cursor = next;
    }

  schedulerMoveDummy (sched);
}

void
schedulerMoveDummy (Scheduler *sched)
{
  ssize_t new_dummy_idx = (sched->curr_bucket - 1 + sched->num_buckets + 1)
                          % (sched->num_buckets + 1);
  EventBucket *b = &sched->buckets[new_dummy_idx];

  EventNotice *cursor = b->anchor.next;
  while (cursor != &b->anchor)
    {
      EventNotice *next = cursor->next;
      noticeListUnlink (cursor);
      memDeallocSafe (cursor);
      cursor = next;
    }

  b->count = 0;
  b->key = sched->lower_bound + sched->num_buckets * sched->interval_width;
  b->is_dummy = true;

  sched->lower_bound += sched->interval_width;
  sched->curr_bucket = (sched->curr_bucket + 1) % (sched->num_buckets + 1);
}

void
schedulerAdjust (Scheduler *sched, ssize_t idx)
{
  size_t next_idx = (idx + 1) % (sched->num_buckets + 1);
  EventBucket *left = &sched->buckets[idx];
  EventBucket *right = &sched->buckets[next_idx];

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

void
schedulerExecuteLoop (Scheduler *sched)
{
  int tfd = timerfd_create (CLOCK_REALTIME, 0);
  while (true)
    {
      EventNotice *evt = schedulerRemoveMin (sched);
      time_t now = time (NULL);

      if (evt->time <= now)
        {
          cronjobExecute (evt->job);

          time_t next_time
              = timesetComputeNextOccurence (&evt->job->timeset, now + 60);
          if (next_time != (time_t)TIME_UNSPEC)
            {
              evt->time = next_time;
              schedulerHold (sched, evt, next_time - sched->lower_bound);
            }

          continue;
        }

      struct itimerspec its = (struct itimerspec){
        .it_value.tv_sec = evt->time,
        .it_value.tv_nsec = 0,
      };

      timerfd_settime (tfd, TFD_TIME_ABSTIME, &its, NULL);

      struct pollfd pfd = (struct pollfd){
        .fd = tfd,
        .events = POLLIN,
      };
      while (poll (&pfd, 1, -1) > 0)
        {
          if (pfd.revents & POLLIN)
            {
              uint64_t expirations = 0;
              read (tfd, &expirations, sizeof (expirations));

              now = time (NULL);
              if (evt->time <= now)
                {
                  cronjobExecute (evt->job);

                  time_t next_time = timesetComputeNextOccurence (
                      &evt->job->timeset, now + 60);
                  if (next_time != (time_t)TIME_UNSPEC)
                    {
                      evt->time = next_time;
                      schedulerHold (sched, evt, next_time - sched->lower_bound);
                    }

                  break;
                }
            }
        }
    }

  close (tfd);
}

EventNotice *
noticeNew (time_t time, CronJob *job)
{
  EventNotice *evt = memAllocSafe (sizeof (EventNotice));
  evt->time = time;
  evt->job = job;
  evt->bucket_idx = 0;
  evt->prev = evt->next = NULL;
  return evt;
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
