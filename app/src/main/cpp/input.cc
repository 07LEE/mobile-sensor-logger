#include "input.h"

#include <android/log.h>
#include <game-activity/GameActivity.h>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

// From android/keycodes.h, which the glue does not pull in.
constexpr int32_t kKeycodeVolumeUp = 24;
constexpr int32_t kKeycodeVolumeDown = 25;

constexpr int32_t kActionDown = 0;

// Everything, where the default filter drops exactly the keys this app wants.
bool AcceptEveryKey(const GameActivityKeyEvent*) { return true; }

}  // namespace

void Input::Attach(android_app* app) {
  android_app_set_key_event_filter(app, AcceptEveryKey);
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "input: volume keys enabled (up: lens, down: record)");
}

Action Input::Poll(android_app* app) {
  android_input_buffer* input = android_app_swap_input_buffers(app);
  if (input == nullptr) return Action::kNone;

  Action action = Action::kNone;

  for (uint64_t i = 0; i < input->keyEventsCount; ++i) {
    const GameActivityKeyEvent& event = input->keyEvents[i];
    if (event.action != kActionDown) continue;

    if (event.keyCode == kKeycodeVolumeDown) {
      action = Action::kToggleRecording;
    } else if (event.keyCode == kKeycodeVolumeUp) {
      action = Action::kNextLens;
    }
  }

  // Touches arrive but are deliberately not bound to anything. The phone is
  // held against a scene while filming and a palm across the screen is not a
  // decision; a capture that stops because of one is a trip wasted. The volume
  // keys cannot be pressed by accident in the same way.

  // Reported once per batch rather than per event, so a held key does not fill
  // the log; this is here to answer whether input arrives at all.
  if (input->keyEventsCount > 0 || input->motionEventsCount > 0) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "input: %llu keys, %llu touches",
                        (unsigned long long)input->keyEventsCount,
                        (unsigned long long)input->motionEventsCount);
  }

  android_app_clear_key_events(input);
  android_app_clear_motion_events(input);
  return action;
}

}  // namespace sensor_logger
