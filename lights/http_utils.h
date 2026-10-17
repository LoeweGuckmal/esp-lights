#pragma once

#include <WebServer.h>

// Helpers shared by the HTTP routes of all lights.
// Parameters may come from the query string (?r=255) or a flat JSON body
// ({"r":255}), like the AwoX Python bridge.

// Reads a number parameter and rounds it to an integer.
// Returns true if it is valid (stored in value) or missing (value unchanged).
// If it is present but not a number from minValue to maxValue, sends a
// 400 error and returns false: the handler should just return.
bool readIntParam(WebServer& server, const char* name, int& value,
                  int minValue, int maxValue);

// Sends {"ok":true,<fields>}, e.g. sendOk(server, "\"power\":\"on\"").
void sendOk(WebServer& server, const String& fields);

// Sends {"ok":false,"error":"<message>"}.
void sendError(WebServer& server, int code, const String& message);

// The value as JSON; -1 (unknown / not given) becomes null.
String jsonNumber(int value);

String jsonBool(bool value);
