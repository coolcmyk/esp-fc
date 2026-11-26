// ESP32-P4 DSP support for Hal::Dsp.
//
// The FFT is intentionally a stub: the dynamic notch filter reads all-zero
// magnitudes, which disables it but keeps the control loop running. Everything
// else (allocation, window) is real so the filter code paths are exercised.
// TODO: radix-2 FFT (or esp-dsp component) before flying with dynamic filters.
#include "Hal/Dsp/Fft.hpp"

#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"

namespace Hal::Dsp {

void AlignedDeleter::operator()(float* ptr) const noexcept { heap_caps_free(ptr); }

FloatBuffer allocFloats(size_t count) {
  void* mem = heap_caps_aligned_alloc(16, count * sizeof(float),
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (mem) memset(mem, 0, count * sizeof(float));
  return FloatBuffer(static_cast<float*>(mem));
}

bool Fft::init(size_t size) {
  if (size == 0) return false;
  _size = size;
  return true;
}

void Fft::windowHann(float* dst, size_t size) {
  if (size < 2) {
    if (size == 1) dst[0] = 1.0f;
    return;
  }
  const float k = 2.0f * float(M_PI) / float(size - 1);
  for (size_t i = 0; i < size; ++i) {
    dst[i] = 0.5f * (1.0f - cosf(k * float(i)));
  }
}

void Fft::realForward(float*) {
  // no-op stub, see file header
}

void Fft::magnitude(const float* src, float* dst) {
  (void)src;
  const size_t bins = _size ? _size / 2 : 0;
  for (size_t i = 0; i < bins; ++i) dst[i] = 0.0f;
}

} // namespace Hal::Dsp
