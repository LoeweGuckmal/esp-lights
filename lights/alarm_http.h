#pragma once

#include <WebServer.h>

// HTTP routes for the alarms, same style as the lights: GET or POST,
// parameters in the query string or as a flat JSON body. Every route
// answers with the full alarm status (see /alarm/status).
//   GET  /alarm             alarm page
//        /alarm/status
//        /alarm/on          master switch
//        /alarm/off
//        /alarm/save        id (missing = new), days (bits, 1 = Sunday,
//                           2 = Monday .. 64 = Saturday), hour, minute,
//                           fade (0-60 min), brightness (1-100),
//                           temperature (0 = cold..100 = warm), enabled (0/1),
//                           dark_only (0/1: stays off when it's bright)
//        /alarm/delete      id
//        /alarm/next        date (yyyymmdd, optional) with hour and minute,
//                           or skip=1: changes the next scheduled alarm only
//        /alarm/next/reset  back to the schedule
//        /alarm/once        hour, minute: one-time alarm, light on at once
//        /alarm/once/remove
//        /alarm/snooze      minutes (1-120, default 10): light off now, back
//                           on at once after that. if_alarm=1: only when an
//                           alarm is going on (AlarmClock::alarmGoingOn),
//                           if_dark=1: only while it's dark
//                           (Daylight::darkNow). Otherwise "snoozed":false
//                           and nothing changes
//        /alarm/snooze/cancel
//        /alarm/stop        ends a running sunrise
void registerAlarmRoutes(WebServer& server);
