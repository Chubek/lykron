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
