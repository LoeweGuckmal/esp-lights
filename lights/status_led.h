#pragma once

// The onboard RGB LED shows what isn't connected:
//   red     the room light (after it dropped; not yet after a start)
//   green   the Wi-Fi
//   yellow  both
//   off     all is well
namespace StatusLed {

// These only note the state and may be called from any task.
void setLampDown(bool down);
void setWifiDown(bool down);

// Writes the LED when the state changed. Call from loop(), and from waits
// that keep loop() from running.
void update();

}  // namespace StatusLed
