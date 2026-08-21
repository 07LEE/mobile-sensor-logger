#ifndef SENSOR_LOGGER_INPUT_H
#define SENSOR_LOGGER_INPUT_H

#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace sensor_logger {

// What the person holding the phone asked for.
enum class Action {
  kNone,
  kToggleRecording,
  kNextLens,
};

// Turns key and touch events into actions.
//
// The volume keys rather than the screen. GameActivity's default key filter
// drops them on purpose — a game does not want the volume rocker stolen — so
// this replaces it. They are what a capture can be driven with while the phone
// is being pointed at something: they are found by feel, they work through the
// case, and pressing one does not move the camera the way reaching for a
// particular part of the screen does.
//
// Touches are drained but bound to nothing: a palm across the screen while the
// phone is pointed at something is not a decision, and a capture that stops
// because of one is a trip wasted.
class Input {
 public:
  // Installs the filters. Call once, before the loop.
  void Attach(android_app* app);

  // Drains whatever has arrived since the last call.
  Action Poll(android_app* app);
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_INPUT_H
