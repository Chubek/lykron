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
parserLexInteger (const char *fieldptr)
{
  char buf[MAX_INTEGER + 1] = { 0 };

  for (size_t i = 0; i < MAX_INTEGER && isdigit (*fieldptr); i++)
    buf[i] = *fieldptr++;

  return strtod (&buf[0], NULL);
}

void
parserHandleField (Timeset *ts, const char *fieldptr, TimesetField tsfld)
{
  char chr = 0;
  while (!isblank (*fieldptr))
    {
      chr = *fieldptr;

      if (chr == '*')
        {
          if (LOOKAHEAD (fieldptr) != '/')
            timesetDoGlob (ts, tsfld);
          else
            {
              fieldptr++; // skip asterisk
              fieldptr++; // skip slash
              int step = parserLexInteger (fieldptr);
              timesetDoStep (ts, -1, step, tsfld);
            }
        }
      else if (isdigit (chr))
        {
          int num = parserLexInteger (fieldptr);

          if (*fieldptr == '/')
            {
              fieldptr++;
              int step = parserLexInteger (fieldptr);
              timesetDoStep (ts, num, step, tsfld);
            }
          else if (*fieldptr == '-')
            {
              fieldptr++;
              int range = parserLexInteger (fieldptr);
              timesetDoRange (ts, num, range, tsfld);
            }
          else
            timesetDoIndex (ts, num, tsfld);
        }
      else if (chr == ',')
        fieldptr++;

      fieldptr++;
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

char *
parserHandleUser (const char *lnptr)
{
  // TODO
}

void
parserHandleCommand (const char *lnptr, char **cmdptr, size_t *cmdlenptr)
{
  // TODO
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
        curr_user = parserHandleUser (lnptr);

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
