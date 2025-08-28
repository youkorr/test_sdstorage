#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "image_decoder.h"
#ifdef USE_STORAGE_PNG_SUPPORT
#include <pngle.h>

namespace esphome {
namespace storage {

/**
 * @brief Image decoder specialization for PNG images stored in SD.
 */
class PngDecoder : public ImageDecoder {
 public:
  /**
   * @brief Construct a new PNG Decoder object.
   *
   * @param image The SdImageComponent to decode into.
   */
  PngDecoder(SdImageComponent *image);
  ~PngDecoder() override;

  int prepare(size_t file_size) override;
  int HOT decode(uint8_t *buffer, size_t size) override;

 protected:
  RAMAllocator<pngle_t> allocator_;
  pngle_t *pngle_;
};

}  // namespace storage
}  // namespace esphome

#endif  // USE_STORAGE_PNG_SUPPORT

