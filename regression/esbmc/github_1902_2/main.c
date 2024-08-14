//FormAI DATASET v1.0 Category: Time Travel Simulator ; Style: complex
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main()
{
  time_t current_time;
  struct tm *current_tm;
  int time_travel_year, time_travel_month, time_travel_day;
  time_t time_travel_seconds;
  struct tm *time_travel_tm;
  int current_year, time_travelled_years, age_current, age_time_travelled;

  // Get current time
  current_time = time(NULL);
  current_tm = localtime(&current_time);

  // Print current time
  printf(
    "Current Time: %02d/%02d/%d\n",
    current_tm->tm_mday,
    current_tm->tm_mon + 1,
    current_tm->tm_year + 1900);

  // Get user inputs for time travel date
  printf("Enter year: ");
  scanf("%d", &time_travel_year);
  printf("Enter month: ");
  scanf("%d", &time_travel_month);
  printf("Enter day: ");
  scanf("%d", &time_travel_day);

  // Convert time travel date to seconds
  time_travel_tm = localtime(&current_time);
  time_travel_tm->tm_year = time_travel_year - 1900;
  time_travel_tm->tm_mon = time_travel_month - 1;
  time_travel_tm->tm_mday = time_travel_day;
  time_travel_seconds = mktime(time_travel_tm);

  // Determine current year and years travelled
  current_year = current_tm->tm_year + 1900;
  time_travelled_years = time_travel_year - current_year;

  // Print time travel date and years travelled
  printf(
    "Time Travel Date: %02d/%02d/%d\n",
    time_travel_day,
    time_travel_month,
    time_travel_year);
  printf("Years Travelled: %d\n", time_travelled_years);

  // Determine current age and age at time travel date
  age_current = current_year - 1990;
  age_time_travelled = age_current + time_travelled_years;

  // Print age at current time and at time travel date
  printf("Age at Current Time: %d\n", age_current);
  printf("Age at Time Travel Date: %d\n", age_time_travelled);

  return 0;
}