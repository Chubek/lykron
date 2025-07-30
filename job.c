#include <pwd.h>
#include <sys/types.h>

#include "lykron.h"

time_t
timesetComputeNextOccurance (Timeset *tset, time_t now)
{
  struct tm tm;
  localtime_r (&now, &tm);

  for (size_t i = 0; i < 60 * 24 * 356 * 5; i++)
    {
      time_t candidate = mktime (&tm);
      if (candidate == (time_t)TIME_UNSPEC)
        break;

      int minute = tm.tm_min;
      int hour = tm.tm_hour;
      int mday = tm.tm_mday;
      int mon = tm.tm_mon;
      int wday = tm.tm_wday;

      if (ts->mins[minute] && ts->hours[hour] && ts->month[mon]
          && (ts->dom[mday - 1] || ts->dow[wday]))
        return candidate;

      tm.tm_min++;
    }

  return (time_t)TIME_UNSPEC;
}

CronJob *
cronjobNew (Timeset *ts, const uint8_t *command, size_t command_len,
            const char *user)
{
  CronJob *cj = memAllocSafe (sizeof (CronJob));
  cj->command = strndup (command, command_len);
  cj->command_len = command_len;
  cj->next = NULL;

  memmove (&cj->timeset, ts, sizeof (Timeset));

  if (user == NULL)
    strncat (&cj->user[0], "root", 4);
  else
    strncat (&cj->user[0], user, LOGIN_NAME_MAX);

  struct passwd *pwd = getpwnam (&cj->user[0]);
  cj->uid = pwd->pw_uid;
  cj->gid = pwd->pw_gid;

  return cj;
}

void
cronjobListDelete (CronJob *cj)
{
  if (cj == NULL)
    return;

  CronJob *next = cj->next;

  memDeallocSafe (cj->command);
  memDeallocSafe (cj);

  cronjobDelete (next);
}

void
cronjobListLink (CronJob *hcj, CronJob *ncj)
{
  CronJob *tcj = NULL;
  for (tcj = hcj; tcj->next; tcj = tcj->next)
    ;
  tcj->next = ncj;
}

void
cronjobQueue (CronJob *cj, Interval *ival)
{
  time_t now = time (NULL);
  time_t next_occur = timesetComputeNextOccurance (&cj->timeset, now);

  EventNotice *evt = noticeNew (next_occur, cj->command, cj->command_len);

  time_t delay = next_occur - ival->lower_bound;
  intervalHold (ival, evt, delay);
}
