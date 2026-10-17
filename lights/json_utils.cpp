#include "json_utils.h"

bool getJsonInt(const String& body, const char* key, int& value,
                int minValue, int maxValue) {
  String search = String("\"") + key + "\"";
  int pos = body.indexOf(search);

  if (pos < 0) return false;

  pos = body.indexOf(':', pos + search.length());
  if (pos < 0) return false;

  pos++;
  while (pos < body.length() &&
         (body[pos] == ' ' || body[pos] == '\t' ||
          body[pos] == '\r' || body[pos] == '\n')) {
    pos++;
  }

  const char* start = body.c_str() + pos;
  char* end = nullptr;
  long parsed = strtol(start, &end, 10);

  if (end == start) return false;

  // The next character must be a JSON value delimiter.
  while (*end == ' ' || *end == '\t' ||
         *end == '\r' || *end == '\n') {
    end++;
  }

  if (*end != ',' && *end != '}' && *end != '\0') {
    return false;
  }

  if (parsed < minValue || parsed > maxValue) return false;

  value = (int)parsed;
  return true;
}
