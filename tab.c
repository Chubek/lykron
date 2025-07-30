#define POSIX_SOURCE
#define POSIX_C_SOURCE
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lykron.h"

CronTab *
crontabNew (const char *path, const char *user, bool is_main)
{
  CronTab *ct = memAllocSafe (sizeof (CronTab));
  ct->path = strndup (&ct->path[0], path, PATH_MAX);
  ct->user = strndup (&ct->user[0], user, LOGIN_NAME_MAX);
  ct->first_job = NULL;
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
crontabIsModifiedMtime (CronTab *ct)
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

void
crontabWatchMtime (CronTab *ctlst)
{
  for (CronTab *tct = ctlst; tct; tct = tct->next)
    if (crontabIsModifiedMtime (tct))
      crontabReload (ctlst, &tct->path[0]);
}

void
crontabWatchInotify (CronTab *ctlst)
{
  int wd = 0;
  struct pollfd pfd;
  int inotfd = inotify_init1 (IN_NONBLOCK);
  if (inotfd < 0)
    errorOut ("inotify_init");

  for (size_t i = 0; TABLE_DIRS[i] != NULL; i++)
    wd = inotify_add_watch (inotfd, TABLE_DIRS[i],
                            IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM
                                | IN_MOVED_TO);

  if (wd < 0)
    errorOut ("inotify_add_watch");

  pfd = (struct pollfd){
    .fd = inotfd,
    .events = POLLIN,
  };
  if (poll (&pfd, 1, -1) && (pfd.revents & POLLIN))
    {
      char buf[MAX_BUF] = { 0 };
      ssize_t n_read = read (inotfd, buf, sizeof (buf));
      if (n_read < 0)
        errorOut ("read");

      for (char *ptr = &buf[0], ptr < buf + n_read;)
        {
          struct inotify_event *evt = (struct inotify_event *)ptr;

          if (evt->mask & IN_MODIFY || evt->mask & IN_CREATE
              || evt->mask & IN_MOVED_TO)
            crontabReload (ctlst, evt->name);

          ptr += sizeof (struct inotify_event) + evt->len;
        }
    }
}

CronTab *
crontabLoadAll (void)
{
  // TODO
}

void
crontabReload (CronTab *ctlst, const char *path_key)
{
  // TODO
}
