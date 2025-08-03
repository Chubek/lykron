#define POSIX_SOURCE
#define POSIX_C_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lykron.h"

Symtbl *
symtblNew (void)
{
  Symtbl *stab = memAllocSafe (sizeof (Symtbl));
  stab->symbols = memAllocBlockSafe (INIT_SYMTBL_SIZE, sizeof (struct Symbol));
  stab->num_symbols = 0;
  stab->max_symbols = INIT_SYMTBL_SIZE;
  stab->log2 = INIT_SYMTBL_LOG2;

  return stab;
}

void
symtblDelete (Symtbl *stab)
{
  for (size_t i = 0; i < stab->max_symbols; i++)
    {
      if (stab->symbols[i].occupied)
        {
          memDeallocSafe (stab->symbols[i].key);
          memDeallocSafe (stab->symbols[i].value);
        }
    }

  memDeallocSafe (stab->symbols);
  memDeallocSafe (stab);
}

void
symtblSet (Symtbl *stab, const uint8_t *key, size_t key_len, uint8_t *value,
           size_t value_len)
{
  if (stab->num_symbols + 1 >= stab->max_symbols)
    {
      size_t old_max_symbols = stab->max_symbols;
      stab->max_symbols <<= 1;
      stab->log2 += 1;
      stab->symbols
          = memReallocSafe (stab->symbols, old_max_symbols, stab->max_symbols,
                            sizeof (struct Symbol));
    }

  size_t idx = _knuth_hash32 (key, stab->log2);
  if (stab->symbols[idx].occupied)
    {
      memDeallocSafe (stab->symbols[idx].value);
      stab->symbols[idx].value = strndup (value, value_len);
    }
  else
    {
      stab->symbols[idx].key = strndup (key, key_len);
      stab->symbols[idx].value = strndup (value, value_len);
      stab->symbols[idx].occupied = true;
      stab->num_symbols++;
    }
}

char *
symtblGet (Symtbl *stab, const uint8_t *key)
{
  size_t idx = _fnv1a_hash32 (key) % stab->max_symbols;
  if (idx >= stab->max_symbols || !stab->symbols[idx].occupied)
    return NULL;
  else
    return stab->symbols[idx].value;
}

char **
symtblGetEnvironPointer (Symtbl *stab)
{
  char **environ = memAllocBlockSafe (stab->num_symbols + 1, sizeof (char *));
  size_t env_n = 0;
  for (size_t i = 0; i < stab->max_symbols; i++)
    {
      if (!stab->symbols[i].occupied)
        continue;
      size_t key_len = strlen (stab->symbols[i].key);
      size_t val_len = strlen (stab->symbols[i].value);
      environ[env_n]
          = memAllocBlockSafe (key_len + val_len + 2, sizeof (char));
      strncat (environ[env_n], stab->symbols[i].key, key_len);
      strncat (environ[env_n], "=", 1);
      strncat (environ[env_n], stab->symbols[i].value, val_len);
      env_n++;
    }
  environ[stab->num_symbols] = NULL;
  return environ;
}

CronTab *
crontabNew (const char *path, const char *user, bool is_main)
{
  CronTab *ct = memAllocSafe (sizeof (CronTab));
  ct->path = strndup (&ct->path[0], path, PATH_MAX);
  ct->user = strndup (&ct->user[0], user, LOGIN_NAME_MAX);
  ct->first_job = NULL;
  ct->is_main = is_main;
  ct->sched = schedulerNew ();
  ct->logger = loggerNew ();
  ct->stab = symtblNew ();
  ct->next = NULL;

  struct stat st = { 0 };
  if (stat (&ct->path[0], &st) < 0)
    _err_out ("stat");

  ct->mtime = st.st_mtim.tv_sec;
}

void
crontabListDelete (CronTab *ct)
{
  if (ct == NULL)
    return;

  CronTab *next = ct->next;
  schedulerDelete (ct->scheduler);
  loggerDelete (ct->logger);
  cronjobListDelete (ct->first_job);
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
    _err_out ("stat");

  if (ct->mtime < st.st_mtim.tv_sec)
    {
      ct->mtime = st.st_mtim.tv_sec;
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
    _err_out ("inotify_init");

  for (size_t i = 0; TABLE_DIRS[i] != NULL; i++)
    wd = inotify_add_watch (inotfd, TABLE_DIRS[i],
                            IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM
                                | IN_MOVED_TO);

  if (wd < 0)
    _err_out ("inotify_add_watch");

  pfd = (struct pollfd){
    .fd = inotfd,
    .events = POLLIN,
  };
  if (poll (&pfd, 1, -1) && (pfd.revents & POLLIN))
    {
      char buf[MAX_BUF] = { 0 };
      ssize_t n_read = read (inotfd, buf, sizeof (buf));
      if (n_read < 0)
        _err_out ("read");

      for (char *ptr = &buf[0]; ptr < buf + n_read;)
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
  const char *path = NULL;
  CronTab *ctlst = crontabLoadFromFile (TABLE_FILE_SYSWIDE, true);
  for (size_t i = 0; TABLE_DIRS[i] != NULL; i++)
    {
      path = TABLE_DIRS[i];
      DIR *dir = opendir (path);
      if (dir == NULL)
        _err_out ("opendir");

      struct dirent *entry;
      while ((entry = readdir (dir)) != NULL)
        {
          if (entry->d_type == DT_REG)
            {
              char *joined_path = _path_join (path, entry->d_name);
              CronTab *ct = crontabLoadFromFile (joined_path, false);
              crontabListLink (ctlst, ct);
              memDeallocSafe (joined_path);
            }
        }

      closedir (dir);
    }

  return ctlst;
}

void
crontabReload (CronTab *ctlst, const char *path_key)
{
  for (CronTab *tct = ctlst; tct; tct = tct->next)
    if (strncmp (tct->path, path_key, PATH_MAX))
      tct = crontabLoadFromFile (tct->path, tct->is_main);
}

CronTab *
crontabLoadFromFile (const char *path, bool is_main)
{
  char user[LOGIN_NAME_MAX + 1] = { 0 }, *userp = NULL;
  CronTab *ct = NULL;

  if (!is_main)
    {
      if (gethostname (&user[0], LOGIN_NAME_MAX) < 0)
        _err_out ("gethostname");
      userp = &user[0];
    }

  ct = crontabNew (path, userp, is_main);
  parserParseTable (ct);

  return ct;
}
