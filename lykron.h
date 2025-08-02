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

#ifndef CROND_PID_FILE
#define CROND_PID_FILE "/run/lykron.pid"
#endif

#ifndef INIT_SYMTBL_SIZE
#define INIT_SYMTBL_SIZE 1024
#endif

#ifndef INIT_SYMTBL_LOG2
#define INIT_SYMTBL_LOG2 10
#endif

#define PHI 0x5851f42dULL

#define MAX_BUF 4096
#define MAX_ID 16
#define ARGC_DFL 32

#define NLIM 32
#define TIME_UNSPEC (time_t)-1

#define NUM_Mins 60
#define NUM_Hours 24
#define NUM_DoM 32
#define NUM_Month 13
#define NUM_DoW 7

#define LOOKAHEAD(sptr) (*(sptr + 1))

#define SKIP_Whitespace(ptr)                                                  \
  do                                                                          \
    {                                                                         \
      while (isblank (*ptr))                                                  \
        ptr++;                                                                \
    }                                                                         \
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

  struct CronJob *next;
} CronJob;

typedef struct Symtbl
{
  struct Symbol
  {
    const uint8_t *key;
    uint8_t *value;
    bool occupied;
  } *symbols;
  size_t num_symbols;
  size_t max_symbols;
} Symtbl;

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

typedef struct Scheduler
{
  EventBucket *buckets;
  size_t num_buckets;
  size_t curr_bucket;
  time_t lower_bound;
  time_t interval_width;
} Scheduler;

typedef struct CronTab
{
  const char path[PATH_MAX + 1];
  const char user[LOGIN_NAME_MAX + 1];
  time_t mtime;

  Symtbl *stab;
  Scheduler *sched;
  CronJob *first_job;
  struct CronTab *next;
} CronTab;

typedef struct Logger
{
  const char *mail_from;
  const char *mail_to;
  bool syslog;
} Logger;

typedef struct Daemon
{
  Logger logger;
  CronTab *first_tab;
} Daemon;

typedef enum
{
  LINE_Comment,
  LINE_Directive,
  LINE_Assign,
  LINE_Field,
  LINE_None,
} LineKind;

typedef enum
{
  TSFIELD_Mins = 0,
  TSFIELD_Hours = 1,
  TSFIELD_DoM = 2,
  TSFIELD_Month = 3,
  TSFIELD_DoW = 4,
  TSFIELD_TimesetField = 5,
} TimesetField;

static const char *TABLE_DIRS[] = {
  "/etc/cron.d/",
  "/var/spool/cron/",
  TABLE_DIRS_ADDITIONAL,
  NULL,
};

static inline char *
_path_join (const char *ph, char *pt)
{
  char *pj = memAllocBlockSafe (PATH_MAX * 2, sizeof (char));
  strncat (pj, ph, PATH_MAX);
  strncat (pj, pt, PATH_MAX);

  return pj;
}

static inline uint32_t
_fnv1a_hash32 (const uint8_t *data)
{
  uint32_t hash = 0x811c9dc5u;
  uint8_t chr = 0;

  while ((chr = *data++))
    {
      hash ^= chr;
      hash *= 0x01000193u;
    }

  return hash;
}

static inline uint32_t
_knuth_hash32 (const uint8_t *data)
{
  return (uint32_t)(PHI * (uint64_t)_fnv1a_hash32 (data));
}

static inline void
_free_envptr (const char **envp)
{
  for (char *e = *envp; e; e++)
    memDeallocSafe (e);
  memDeallocSafe (envp);
}

static inline char *
_get_tmp_dir (void)
{
  const char *tmpdir = getenv ("TMPDIR");
  if (tmpdir == NULL)
    tmpdir = P_tmpdir;
  return tmpdir;
}

#endif
