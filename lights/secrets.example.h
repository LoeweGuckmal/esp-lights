#pragma once

// Copy this file to secrets.h and fill in your Wi-Fi credentials.
// secrets.h is ignored by git, so your password never ends up in a commit.

constexpr char WIFI_SSID[] = "SSID";
constexpr char WIFI_PASSWORD[] = "PASSWORD";

// Mesh credentials of the room light (AwoX), max 16 characters each.
// Same values as MESH_NAME / MESH_PASSWORD in the AwoX Python project.
constexpr char ROOM_LIGHT_MESH_NAME[] = "MESH_NAME";
constexpr char ROOM_LIGHT_MESH_PASSWORD[] = "MESH_PASSWORD";

// Your location, for the weather forecast that switches the room light
// (daylight.h). Two decimals (about 1 km) are plenty.
constexpr char WEATHER_LATITUDE[] = "52.52";
constexpr char WEATHER_LONGITUDE[] = "13.41";
