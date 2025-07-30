#define POSIX_SOURCE
#define POSIX_C_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lykron.h"

CronTab *
crontabNew (const char *path, const char *user, bool is_main)
{
  CronTab *ct = memAllocSafe (sizeof (CronTab));
  ct->path = strndup (&ct->path[0], path, PATH_MAX);
  ct->user = strndup (&ct->user[0], user, LOG_NAME_MAX);
  ct->is_main = is_main;
  ct->next = NULL;

  struct stat st = { 0 };
  if (stat (&ct->path[0], &st) < 0)
    errorOut ("stat");

  ct->mtime = st.st_mtim.tv_sec;
}

void
crontabListDelete (CronTab *ct)
{
  if (ct == NULL)
    return;

  CronTab *next = ct->next;
  memDeallocSafe (ct);

  crontabListDelete (next);
}

void
crontabListLink (CronTab *hct, CronTab *nct)
{
  CronTab *tct = NULL;
  for (tct = hct; tct->next; tct = tct->next)
    ;
  tct->next = nct;
}

bool
crontabIsModified (CronTab *ct)
{
  struct stat st;
  if (stat (&ct->path[0], &st) < 0)
    errorOut ("stat");

  if (ct->mtime < st.st_mtim.tv_sec)
    {
      ct - mtime = st.st_mtim.tv_sec;
      return true;
    }

  return false;
}
