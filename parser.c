#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "lykron.h"

static inline LineKind
parserAssessLineKind (const char *ln)
{
  if (ln == NULL || isblank (ln[0]))
    return LINE_None;
  else if (ln[0] == '#')
    return LINE_Comment;
  else if (ln[0] == '@')
    return LINE_Directive;
  else if (isdigit (ln[0]) || ln[0] == '*')
    return LINE_Field;
  else if (isalpha (ln[0]))
    return LINE_Assign;
  else
    raiseSyntaxError ("Unkown line");
}

int
parserLexInteger (const char *lnptr)
{
  char buf[MAX_INTEGER + 1] = { 0 };

  for (size_t i = 0; i < MAX_INTEGER && isdigit (*lnptr); i++)
    buf[i] = *lnptr++;

  return strtod (&buf[0], NULL);
}

void
parserHandleField (Timeset *ts, const char *lnptr, TimesetField tsfld)
{
  char chr = 0;
  while (!isblank (*lnptr))
    {
      chr = *lnptr;

      if (chr == '*')
        {
          if (LOOKAHEAD (lnptr) != '/')
            timesetDoGlob (ts, tsfld);
          else
            {
              lnptr++; // skip asterisk
              lnptr++; // skip slash
              int step = parserLexInteger (lnptr);
              timesetDoStep (ts, -1, step, tsfld);
            }
        }
      else if (isdigit (chr))
        {
          int num = parserLexInteger (lnptr);

          if (*lnptr == '/')
            {
              lnptr++;
              int step = parserLexInteger (lnptr);
              timesetDoStep (ts, num, step, tsfld);
            }
          else if (*lnptr == '-')
            {
              lnptr++;
              int range = parserLexInteger (lnptr);
              timesetDoRange (ts, num, range, tsfld);
            }
          else
            timesetDoIndex (ts, num, tsfld);
        }
      else if (chr == ',')
        lnptr++;

      lnptr++;
    }
}

void
parserHandleFields (Timeset *ts, const char *lnptr)
{
  static TimesetFiled tsflds[TSFIELD_TimesetField] = {
    TSFIELD_Mins,  TSFIELD_Hours, TSFIELD_DoM,
    TSFIELD_Month, TSFIELD_DoW,   TSFIELD_TimesetField,
  };

  for (size_t i = 0; tsflds[i] != TSFIELD_TimesetField; i++)
    {
      SKIP_Whitespace (lnptr);
      parserHandleField (ts, lnptr, tsflds[i])
    }
}

void
parserHandleDirective (Timeset *ts, const char *lnptr)
{
  char dir[MAX_DIRECTIVE_LEN + 1] = { 0 };
  for (size_t i = 0; i < MAX_DIRECTIVE_LEN && !isblank (*lnptr); i++)
    dir[i] = *lnptr++;

  if (strncmp (dir, "@reboot", MAX_DIRECTIVE_LEN))
    timesetDoReboot (ts);
  else if (strncmp (dir, "@yearly", MAX_DIRECTIVE_LEN))
    timesetDoYearly (ts);
  else if (strncmp (dir, "@annually", MAX_DIRECTIVE_LEN))
    timesetDoAnually (ts);
  else if (strncmp (dir, "@monthly", MAX_DIRECTIVE_LEN))
    timesetDoMonthly (ts);
  else if (strncmp (dir, "@weekly", MAX_DIRECTIVE_LEN))
    timesetDoWeekly (ts);
  else if (strncmp (dir, "@daily", MAX_DIRECTIVE_LEN))
    timesetDoDaily (ts);
  else if (strncmp (dir, "@hourly", MAX_DIRECTIVE_LEN))
    timesetDoHourly (ts);
  else
    raiseError ("Unknown directive");
}

void
parserHandleAssign (Symtbl *stab, const char *lnptr)
{
  uint8_t *key = NULL;
  uint8_t *value = NULL;
  uint8_t *kptr = NULL, *vptr = NULL;
  size_t key_len, val_len;
  wordexp_t wxp;

  for (kptr = lnptr; *kptr != '='; kptr++)
    ;
  key_len = (size_t)(kptr - lnptr);

  for (vptr = ++kptr; *vptr; vptr++)
    ;
  val_len = (size_t)(vptr - kptr);

  key = memAllocBlockSafe (key_len + 1, sizeof (uint8_t));
  value = memAllocBlockSafe (val_len + 1, sizeof (uint8_t));

  strncpy (name, lnptr, key_len);
  strncpy (value, &lnptr[key_len + 1], val_len);

  wordexp (value, &wxp, 0);
  memDeallocSafe (value);
  val_len = strlen (*wxp.we_wordv);
  value = (uint8_t *)strndup (*wxp.we_wordv, val_len);
  wordfree (&wxp);

  symtblSet (stab, key, key_len, value, value_len);

  memDeallocSafe (key);
  memDeallocSafe (value);
}

void
parserHandleUser (const char *lnptr, char *userptr)
{
  SKIP_Whitesace (lnptr);
  for (size_t i = 0; i < LOGIN_NAME_MAX && !isblank (*lnptr); i++)
    userptr[i] = *lnptr++;
}

void
parserHandleCommand (const char *lnptr, char **cmdptr, size_t *cmdlenptr)
{
  SKIP_Whitespace (lnptr);
  *cmdlenptr = strlen (lnptr);
  *cmdptr = strndup (lnptr, *cmdlenptr);
}

void
parserParseTable (CronTab *ct)
{
  char *ln = NULL;
  size_t ln_len = 0;
  FILE *fstream = fopen (ct->path, "r");
  if (fstream == NULL)
    errorOut ("fopen");

  while (getline (&ln, &ln_len, fstream) > 0)
    {
      if (!ln_len)
        continue;

      LineKind lnknd = parserAssessLineKind (ln);

      if (lnknd == LINE_None || lnknd == LINE_Comment)
        continue;

      if (lnknd == LINE_Assign)
        {
          parserHandleAssign (ct->stab, lnptr);
          continue;
        }

      Timeset curr_ts = { 0 };
      if (lnknd == LINE_Directive)
        parserHandleDirective (&curr_ts, lnptr);
      else if (lnknd == LINE_Field)
        parserHandleFields (&curr_ts, lnptr);

      char *curr_user = &ct->user[0];
      if (ct->is_main)
        parserHandleUser (lnptr, curr_user);

      char *curr_cmd = NULL;
      size_t curr_cmd_len = 0;
      parserHandleCommand (lnptr, &curr_cmd, &curr_cmd_len);

      CronJob *curr_cj
          = cronjobNew (&curr_ts, curr_cmd, curr_cmd_len, curr_user);
      if (ct->jobs == NULL)
        ct->jobs = curr_cj;
      else
        cronjobListLink (ct->jobs, curr_cj);
    }

  memDeallocSafe (ln);
  fclose (fstream);
}
