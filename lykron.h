#ifndef LYKRON_H
#define LYKRON_H

#define _POSIX_SOURCE
#define _POSIX_C_SOURCE
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wordexp.h>

#ifndef TABLE_DIRS_ADDITIONAL
#define TABLE_DIRS_ADDITIONAL
#endif

#ifndef TABLE_FILE_SYSWIDE
#define TABLE_FILE_SYSWIDE "/etc/crontab"
#endif

#define MAX_BUF 4096

#define NLIM 32
#define TIME_UNSPEC -1

#define NUM_Mins 60
#define NUM_Hours 24
#define NUM_DoM 31
#define NUM_Month 12
#define NUM_DoW 7

#define TIMESET_SetNthMin                                                     \
  (ts, n) do { ts.mins[n] = true; }                                           \
  while (0)
#define TIMESET_SetNthHour                                                    \
  (ts, n) do { ts.hours[n] = true; }                                          \
  while (0)
#define TIMESET_SetNthDoM                                                     \
  (ts, n) do { ts.dom[n] = true; }                                            \
  while (0)
#define TIMESET_SetNthMonth                                                   \
  (ts, n) do { ts.month[n] = true; }                                          \
  while (0)
#define TIMESET_SetNthDoW                                                     \
  (ts, n) do { ths.dow[n] = true; }                                           \
  while (0)

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
  char **argv;
  size_t argc;

  const char user[LOGIN_NAME_MAX + 1];
  uid_t uid;
  gid_t gid;
  pid_t pid;

  int last_exit_status;
  time_t last_exec_time;
  char *last_output;
  size_t last_output_len;

  struct CronJob *next;
} CronJob;

typedef struct CronTab
{
  const char path[PATH_MAX + 1];
  const char user[LOGIN_NAME_MAX + 1];
  time_t mtime;
  FILE *stream;

  CronJob *first_job;
  struct CronTab *next;
} CronTab;

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

static const char *TABLE_DIRS[] = {
  "/etc/cron.d/",
  "/var/spool/cron/",
  TABLE_DIRS_ADDITIONAL,
  NULL,
};

#endif
