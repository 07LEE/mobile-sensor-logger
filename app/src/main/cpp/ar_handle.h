#ifndef SENSOR_LOGGER_AR_HANDLE_H
#define SENSOR_LOGGER_AR_HANDLE_H

#include <arcore_c_api.h>

#include <utility>

namespace sensor_logger {

// Owning handle for an ARCore object.
//
// The C API hands out raw pointers that must each be released through their own
// destroy function, and a missed release stalls the camera rather than failing
// loudly — a frame or image left unreleased starves the pool the next
// acquisition draws from. Every acquired object in this app is held here so
// that release is not a thing anyone has to remember.
template <typename T, void (*Destroy)(T*)>
class ArHandle {
 public:
  ArHandle() = default;

  explicit ArHandle(T* ptr) : ptr_(ptr) {}

  ~ArHandle() { reset(); }

  ArHandle(const ArHandle&) = delete;
  ArHandle& operator=(const ArHandle&) = delete;

  ArHandle(ArHandle&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

  ArHandle& operator=(ArHandle&& other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = std::exchange(other.ptr_, nullptr);
    }
    return *this;
  }

  T* get() const { return ptr_; }

  // For the create/acquire calls, which write through a T**.
  T** receive() {
    reset();
    return &ptr_;
  }

  explicit operator bool() const { return ptr_ != nullptr; }

  void reset() {
    if (ptr_ != nullptr) {
      Destroy(ptr_);
      ptr_ = nullptr;
    }
  }

 private:
  T* ptr_ = nullptr;
};

using ArConfigHandle = ArHandle<ArConfig, ArConfig_destroy>;
using ArFrameHandle = ArHandle<ArFrame, ArFrame_destroy>;
using ArPoseHandle = ArHandle<ArPose, ArPose_destroy>;
using ArPointCloudHandle = ArHandle<ArPointCloud, ArPointCloud_release>;
using ArCameraIntrinsicsHandle =
    ArHandle<ArCameraIntrinsics, ArCameraIntrinsics_destroy>;
using ArImageHandle = ArHandle<ArImage, ArImage_release>;
using ArCameraConfigHandle = ArHandle<ArCameraConfig, ArCameraConfig_destroy>;
using ArCameraConfigListHandle =
    ArHandle<ArCameraConfigList, ArCameraConfigList_destroy>;
using ArCameraConfigFilterHandle =
    ArHandle<ArCameraConfigFilter, ArCameraConfigFilter_destroy>;

// ArCamera is not owned by the caller — it is valid only for the frame it came
// from and must not be released — so it is deliberately absent here.

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_AR_HANDLE_H
