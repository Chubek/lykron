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

typedef struct TabParser
{

} TabParser;

typedef struct TabLinter
{

} TabLinter;

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

typedef struct CronTab
{
  const char on_disk[PATH_MAX + 1];
  time_t mtime;
  TabParser *parser;
  TabLinter *linter;
} CronTab;

typedef struct Environ
{
  struct Symbol
  {
    const uint8_t *name;
    const uint8_t *value;
  } *symbols;
  size_t num_symbols;

  char **original_environ;
  const char shell[PATH_MAX + 1];

  const char (*tab_dirs)[PATH_MAX + 1];
  size_t num_tab_dirs;

  CronTab *tabs;
  size_t num_tabs;

  CronJob *jobs;
  size_t num_jobs;
} Environ;

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
