#include "http_utils.h"

#include <math.h>

namespace {

// Returns the raw text of the parameter, or false if it is missing.
bool findParam(WebServer& server, const char* name, String& text) {
  if (server.hasArg(name)) {
    text = server.arg(name);
    return true;
  }

  // JSON body: find "name": and take everything up to the next , or }
  String body = server.arg("plain");
  String search = String("\"") + name + "\"";
  int pos = body.indexOf(search);
  if (pos < 0) return false;

  pos = body.indexOf(':', pos + search.length());
  if (pos < 0) return false;

  int end = pos + 1;
  while (end < (int)body.length() && body[end] != ',' && body[end] != '}') {
    end++;
  }

  text = body.substring(pos + 1, end);
  return true;
}

}  // namespace

bool readIntParam(WebServer& server, const char* name, int& value,
                  int minValue, int maxValue) {
  String text;
  if (!findParam(server, name, text)) return true;

  text.trim();
  char* end = nullptr;
  double parsed = strtod(text.c_str(), &end);
  bool isNumber = !text.isEmpty() && *end == '\0' && isfinite(parsed);
  long rounded = isNumber ? lround(parsed) : 0;

  if (!isNumber || rounded < minValue || rounded > maxValue) {
    sendError(server, 400,
              String("Parameter '") + name + "' must be a number from " +
                  minValue + " to " + maxValue);
    return false;
  }

  value = (int)rounded;
  return true;
}

void sendOk(WebServer& server, const String& fields) {
  server.send(200, "application/json", "{\"ok\":true," + fields + "}");
}

void sendError(WebServer& server, int code, const String& message) {
  server.send(code, "application/json",
              "{\"ok\":false,\"error\":\"" + message + "\"}");
}

String jsonNumber(int value) {
  return value < 0 ? String("null") : String(value);
}

String jsonBool(bool value) {
  return value ? "true" : "false";
}
