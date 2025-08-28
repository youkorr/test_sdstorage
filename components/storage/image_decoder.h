#pragma once
#include "esphome/core/color.h"

namespace esphome {
namespace storage {

enum DecodeError : int {
  DECODE_ERROR_INVALID_TYPE = -1,
  DECODE_ERROR_UNSUPPORTED_FORMAT = -2,
  DECODE_ERROR_OUT_OF_MEMORY = -3,
};

class SdImageComponent;

/**
 * @brief Class to abstract decoding different image formats for SD images.
 */
class ImageDecoder {
 public:
  /**
   * @brief Construct a new Image Decoder object
   *
   * @param image The SdImageComponent to decode into.
   */
  ImageDecoder(SdImageComponent *image) : image_(image) {}
  virtual ~ImageDecoder() = default;

  /**
   * @brief Initialize the decoder.
   *
   * @param file_size The total number of bytes of the image file.
   * @return int      Returns 0 on success, a {@see DecodeError} value in case of an error.
   */
  virtual int prepare(size_t file_size) {
    this->download_size_ = file_size;
    return 0;
  }

  /**
   * @brief Decode a part of the image.
   *
   * @param buffer The buffer to read from.
   * @param size   The maximum amount of bytes that can be read from the buffer.
   * @return int   The amount of bytes read. Negative in case of a decoding error.
   */
  virtual int decode(uint8_t *buffer, size_t size) = 0;

  /**
   * @brief Request the image to be resized once the actual dimensions are known.
   *
   * @param width The image's width.
   * @param height The image's height.
   * @return true if resized, false otherwise.
   */
  bool set_size(int width, int height);

  /**
   * @brief Fill a rectangle on the display_buffer using the defined color.
   *
   * @param x The left-most coordinate.
   * @param y The top-most coordinate.
   * @param w The width of the rectangle.
   * @param h The height of the rectangle.
   * @param color The fill color
   */
  void draw(int x, int y, int w, int h, const Color &color);

  bool is_finished() const { return this->decoded_bytes_ == this->download_size_; }

 protected:
  SdImageComponent *image_;
  size_t download_size_ = 1;  // Initial value
  size_t decoded_bytes_ = 0;
  double x_scale_ = 1.0;
  double y_scale_ = 1.0;
};

class DownloadBuffer {
 public:
  DownloadBuffer(size_t size);

  virtual ~DownloadBuffer() { this->allocator_.deallocate(this->buffer_, this->size_); }

  uint8_t *data(size_t offset = 0);

  uint8_t *append() { return this->data(this->unread_); }

  size_t unread() const { return this->unread_; }
  size_t size() const { return this->size_; }
  size_t free_capacity() const { return this->size_ - this->unread_; }

  size_t read(size_t len);
  size_t write(size_t len) {
    this->unread_ += len;
    return this->unread_;
  }

  void reset() { this->unread_ = 0; }

  size_t resize(size_t size);

 protected:
  RAMAllocator<uint8_t> allocator_{};
  uint8_t *buffer_;
  size_t size_;
  size_t unread_;
};

}  // namespace storage
}  // namespace esphome
