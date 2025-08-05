#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lykron.h"

time_t
timesetComputeNextOccurence (Timeset *ts, time_t now)
{
  struct tm tm;
  localtime_r (&now, &tm);

  for (size_t i = 0; i < 60 * 24 * 356 * 5; i++)
    {
      time_t candidate = mktime (&tm);
      if (candidate == TIME_UNSPEC)
        break;

      int minute = tm.tm_min;
      int hour = tm.tm_hour;
      int mday = tm.tm_mday;
      int mon = tm.tm_mon;
      int wday = tm.tm_wday;

      if (ts->mins[minute] && ts->hours[hour] && ts->month[mon]
          && (ts->dom[mday] || ts->dow[wday]))
        return candidate;

      tm.tm_min++;
    }

  return TIME_UNSPEC;
}

bool *
timesetGetFieldOffset (Timeset *ts, TimesetField field)
{
  switch (field)
    {
    case TSFIELD_Mins:
      return &ts->mins[0];
    case TSFIELD_Hours:
      return &ts->hours[0];
    case TSFIELD_DoM:
      return &ts->dom[0];
    case TSFIELD_Month:
      return &ts->month[0];
    case TSFIELD_DoW:
      return &ts->dow[0];
    default:
      return NULL;
    }
}
void
timesetDoGlob (Timeset *ts, int step, TimesetField field)
{
  bool *field = timesetGetFieldOffset (ts, field);
  if (step == -1)
    memset (field, true, TSFIELD_NUMS_LUT[field] * sizeof (bool));
  else
    for (size_t i = 0; i < TSFIELD_NUMS_LUT[field], i += step)
      field[i] = true;
}

void
timesetDoList (Timeset *ts, int *lst, size_t lstlen, TimesetField field)
{
  bool *field = timesetGetFieldOffset (ts, field);
  for (size_t i = 0; i < lstlen; i++)
    {
      int elt = lst[i];
      if (MARK_UpperBitIsSet (elt))
        {
          int lower = RANGE_GetLower (elt);
          int upper = RANGE_GetUpper (elt);

          if (upper - lower > TSFIELD_NUMS_LUT[field])
            _err_out ("Field out of range");

          memset (&field[lower], true, (upper - lower) * sizeof (bool));
        }
      else
        field[elt] = true;
    }
}

void
timesetDoReboot (Timeset *ts)
{
  // TODO
}

void
timesetDoYearly (Timeset *ts)
{
  ts->dom[1] = true;
  ts->month[1] = true;
}

void
timesetDoMonthly (Timeset *ts)
{
  ts->dom[1] = true;
}

void
timesetDoWeekly (Timeset *ts)
{
  ts->mins[0] = true;
  ts->hours[0] = true;
  ts->dow[0] = true;
}

void
timesetDoDaily (Timeset *ts)
{
  ts->mins[0] = true;
  ts->hours[0] = true;
}

void
timesetDoHourly (Timeset *ts)
{
  ts->mins[0] = true;
}

CronJob *
cronjobNew (Timeset *ts, const uint8_t *command, size_t command_len,
            const char *user)
{
  CronJob *cj = memAllocSafe (sizeof (CronJob));
  cj->command = strndup (command, command_len);
  cj->command_len = command_len;
  cj->argv = NULL;
  cj->next = NULL;

  memCopySafe (&cj->timeset, ts, sizeof (Timeset));
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

  for (size_t i = 0; i < cj->argc; i++)
    memDeallocSafe (cj->argv[i]);

  memDeallocSafe (cj->argv);
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
cronjobExecute (CronJob *cj, char **envptr)
{
  if (cj->argv == NULL)
    cronjobPrepCommand (cj);
  if ((cj->pid = fork ()) < 0)
    _err_out ("fork");

  if (cj->pid == 0)
    {
      if (setuid (cj->uid) < 0)
        _err_out ("setuid");
      if (setgid (cj->gid) < 0)
        _err_out ("setgid");

      char path_stdout[MAX_PATH + 1] = { 0 };
      char path_stderr[MAX_PATH + 1] = { 0 };
      ssprintf (&path_stdout[0], "%s/%d.out", _get_tmp_dir (), getpid ());
      ssprintf (&path_stderr[0], "%s/%d.err", _get_tmp_dir (), getpid ());
      freopen (path_stdout, "w", stdout);
      freopen (path_stderr, "w", stderr);

      execvpe (cj->argv[0], &cj->argv[1], envptr);
      _exit (EXIT_FAILURE);
    }
}

void
cronjobScheduleInit (Scheduler *sched, CronJob *cj)
{
  if (cj == NULL)
    return;

  time_t now = time (NULL);
  time_t next_time = timesetComputeNextOccurence (&cj->timeset, now);

  if (next_time == TIME_UNSPEC)
    _err_out ("Could not schedule");

  EventNotice *evt = noticeNew (next_time, cj);
  time_t delay = next_time - sched->lower_bound;
  schedulerHold (sched, evt, delay);

  cronjobScheduleInit (sched, cj->next);
}

void
cronjobPrepCommand (CronJob *cj)
{
  char *cmddup = strndup (cj->command, cj->command_len);
  char *subtok = strtok (cmddup, "\t ");
  size_t max_argc = ARGC_DFL;
  cj->argv = memAllocBlockSafe (ARGC_DFL, sizeof (char *));
  while (subtok != NULL)
    {
      if (cj->argc + 1 >= max_argc)
        {
          size_t old_max_argc = max_argc;
          max_argc += ARGC_DFL;
          cj->argv = memReallocSafe (cj->argv, old_max_argc, max_argc,
                                     sizeof (char *));
        }
      cj->argv[cj->argc++] = strdup (subtok);
      subtok = strtok (NULL, "\t ");
    }
  memDeallocSafe (cmddup);
}
