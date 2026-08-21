#include "input.h"

#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/GameActivityEvents.h>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

// From android/keycodes.h, which the glue does not pull in.
constexpr int32_t kKeycodeVolumeUp = 24;
constexpr int32_t kKeycodeVolumeDown = 25;

constexpr int32_t kActionDown = 0;
constexpr int32_t kMotionActionUp = 1;
constexpr int32_t kMotionActionMask = 0xff;

// Everything, where the default filter drops exactly the keys this app wants.
bool AcceptEveryKey(const GameActivityKeyEvent*) { return true; }

}  // namespace

void Input::Attach(android_app* app) {
  android_app_set_key_event_filter(app, AcceptEveryKey);
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "input: volume keys enabled (up: lens, down: record)");
}

InputEvents Input::Poll(android_app* app) {
  InputEvents events;

  android_input_buffer* input = android_app_swap_input_buffers(app);
  if (input == nullptr) return events;

  for (uint64_t i = 0; i < input->keyEventsCount; ++i) {
    const GameActivityKeyEvent& event = input->keyEvents[i];
    if (event.action != kActionDown) continue;

    if (event.keyCode == kKeycodeVolumeDown) {
      events.action = Action::kToggleRecording;
    } else if (event.keyCode == kKeycodeVolumeUp) {
      events.action = Action::kNextLens;
    }
  }

  // Reported on release rather than on press, so a finger that lands and slides
  // off is not the same as one that lands and lifts.
  for (uint64_t i = 0; i < input->motionEventsCount; ++i) {
    const GameActivityMotionEvent& event = input->motionEvents[i];
    if ((event.action & kMotionActionMask) != kMotionActionUp) continue;
    if (event.pointerCount == 0) continue;

    events.touched = true;
    events.x = GameActivityPointerAxes_getX(&event.pointers[0]);
    events.y = GameActivityPointerAxes_getY(&event.pointers[0]);
  }

  android_app_clear_key_events(input);
  android_app_clear_motion_events(input);
  return events;
}

}  // namespace sensor_logger
