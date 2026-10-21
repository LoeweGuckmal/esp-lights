#pragma once

#include <WebServer.h>

// HTTP routes for the presence check, same style as the alarms: GET or POST,
// parameters in the query string or as a flat JSON body. Every route
// answers with the full status (see /presence/status).
//   GET  /presence               presence page
//        /presence/status
//        /presence/on            master switch
//        /presence/off
//        /presence/check         look for the phone now; watch=1 keeps
//                                listening to show the signal strength
//        /presence/stop          ends a running check
//        /presence/pair          ready to pair for PRESENCE_PAIR_WINDOW_S
//        /presence/pair/cancel
//        /presence/forget        forgets the paired phone
void registerPresenceRoutes(WebServer& server);
