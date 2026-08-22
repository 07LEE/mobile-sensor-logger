#ifndef SENSOR_LOGGER_INPUT_H
#define SENSOR_LOGGER_INPUT_H

#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace sensor_logger {

// What the person holding the phone asked for.
enum class Action {
  kNone,
  kToggleRecording,
  kTogglePreview,
};

// One pass over the input queue.
struct InputEvents {
  Action action = Action::kNone;

  // Where the screen was last touched, in pixels from the top left. Whether
  // that means anything is for the caller to decide against what it drew.
  bool touched = false;
  float x = 0.0f;
  float y = 0.0f;
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
// Down records, up shows the picture. Showing the picture is on a key rather
// than the screen because there is nothing to touch while it is hidden.
//
// Touches come back with their position rather than as an action. What a touch
// means depends on what is under it, and only the caller knows that. Nothing
// that ends a capture is ever put behind one: a palm across the screen while
// the phone is pointed at something is not a decision.
class Input {
 public:
  // Installs the filters. Call once, before the loop.
  void Attach(android_app* app);

  // Drains whatever has arrived since the last call.
  InputEvents Poll(android_app* app);
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_INPUT_H
