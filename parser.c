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
parserParseTimeset (Timeset *ts, const char *fieldptr, int offset)
{
  char chr = 0;
  while (!isblank (*fieldptr))
    {
      chr = *fieldptr;

      if (chr == '*')
        {
          if (LOOKAHEAD (fieldptr) != '/')
            timesetGlob (ts, offset);
          else
            {
              fieldptr++; // skip asterisk
              fieldptr++; // skip slash
              int step = parserLexInteger (fieldptr);
              timesetStep (ts, -1, step, offset);
            }
        }
      else if (isdigit (chr))
        {
          int num = parserLexInteger (fieldptr);

          if (*fieldptr == '/')
            {
              fieldptr++;
              int step = parserLexInteger (fieldptr);
              timesetStepMins (ts, num, step, offset);
            }
          else if (*fieldptr == '-')
            {
              fieldptr++;
              int range = parserLexInteger (fieldptr);
              timesetRangeMins (ts, num, range, offset);
            }
          else
            timesetIndexMins (ts, num, offset);
        }
      else if (chr == ',')
        fieldptr++;

      fieldptr++;
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
