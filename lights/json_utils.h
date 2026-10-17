#pragma once

#include <Arduino.h>

// Reads an integer field from a flat JSON object,
// e.g. getJsonInt("{\"r\":255}", "r", r).
// Returns false if the key is missing, not a number,
// or outside minValue..maxValue.
bool getJsonInt(const String& body, const char* key, int& value,
                int minValue = 0, int maxValue = 255);
