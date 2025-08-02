#define POSIX_SOURCE
#define POSIX_C_SOURCE
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.>
#include <sys/wait.h>
#include <unistd.h>

#include "lykron.h"

void
daemonReapChildren (Deamon *dmn)
{
  int sfd = 0;
  ssize_t slen = 0;
  sigset_t mask = { 0 };
  struct signalfd_siginfo fdsi = { 0 };
  struct pollfd pfd = { 0 };

  sigemptyset (&mask);
  sigaddset (&mask, SIGINT);
  sigaddset (&mask, SIGCHLD);
  sigaddset (&mask, SIGQUIT);

  if (sigprocmask (SIG_BLOCK, &mask, NULL) < 0)
    errorOut ("sigprocmask");

  if ((sfd = signalfd (-1, &mask, 0)) < 0)
    errrorOut ("signalfd");

  pfd = (struct pollfd){ .fd = sfd, .events = POLLIN };
  while (poll (&pfd, 1, -1) > 0)
    {
      if (pfd.revents & POLLIN)
        {
          memset (&fdsi, 0, sizeof (fdsi));
          if (read (sfd, &fdsi, sizeof (fdsi)) < 0)
            errorOut ("read");

          if (fdsi.ssi_signo == SIGCHLD)
            {
              pid_t reaped_pid = fdsi.ssi_pid;
              int reaped_exit_stat = fdsi.ssi_status;

              loggerLogReapedChild (&dmn->loggr, reaped_pid, reaped_exit_stat);
            }
        }
    }

  close (sfd);
}

void
loggerLogReapedChild (Logger *lgr, pid_t reaped_pid, reaped_exit_stat)
{
  char path_stdout[PATH_MAX + 1] = { 0 };
  char path_stderr[PATH_MAX + 1] = { 0 };
  ssprintf (&path_stdout[0], "%s/%d.out", _get_tmp_dir (), reaped_pid);
  ssprintf (&path_stderr[0], "%s/%d.err", _get_tmp_dir (), reaped_pid);

  FILE *outtmp = fopen (path_stdout, "r");
  FILE *errtmp = fopen (path_stderr, "r");

  if (outtmp == NULL)
    errorOut ("fopen");
  if (errtmp == NULL)
    errorOut ("fopen");

  char *logln = NULL;
  size_t logln_len = 0;
  while (getline (&logln, &logln_len, outtmp) > 0)
    {
      if (logln_len == 0 || logln == NULL)
        continue;
      loggerLogOut (lgr, reaped_pid, logln, logln_len);
    }
  while (getline (&logln, &logln_len, errtmp) > 0)
    {
      if (logln_len == 0 || logln == NULL)
        continue;
      loggerLogErr (lgr, reaped_pid, logln, logln_len);
    }
  fclose (outtmp);
  fclose (errtmp);

  loggerLogExitStat (lgr, repeaed_pid, reaped_exit_stat);
}
