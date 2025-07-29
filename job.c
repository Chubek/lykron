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
