#ifndef LYKRON_H
#define LYKRON_H

#define _POSIX_SOURCE
#define _POSIX_C_SOURCE
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wordexp.h>

#define NUM_Mins 60
#define NUM_Hours 24
#define NUM_DoM 31
#define NUM_Month 12
#define NUM_DoW 7

typedef struct Timeset
{
  bool mins[NUM_Mins];
  bool hours[NUM_Hours];
  bool dom[NUM_DoM];
  bool month[NUM_Month];
  bool dow[NUM_DoW];
} Timeset;

typedef struct CronJob
{
  Timeset timeset;
  const uint8_t *command;
  size_t command_len;
  const char user[LOGIN_NAME_MAX + 1];
  uid_t uid;
  gid_t gid;
  pid_t pid;
} CronJob;

typedef struct EventNotice
{
  time_t time;
  CronJob *job;
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
