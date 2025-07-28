#ifndef LYKRON_H
#define LYKRON_H

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
  size_t num_notices;
  bool is_dummy;
  EventNotice anchor;
} EventBucket;

typedef struct Interval
{
  EventBucket *buckets;
  size_t num_buckets;
  size_t curr_bucket;
  time_t lower_bound;
  time_t interval_width;
} Interval;

#endif
