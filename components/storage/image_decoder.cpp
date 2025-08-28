#include "image_decoder.h"
#include "storage.h"
#include "esphome/core/log.h"

namespace esphome {
namespace storage {

static const char *const TAG = "storage.decoder";

bool ImageDecoder::set_size(int width, int height) {
  // Since SdImageComponent doesn't have resize_() method like online_image,
  // we'll work with the current dimensions and calculate scale factors
  int current_width = this->image_->get_current_width();
  int current_height = this->image_->get_current_height();
  
  // If no resize is set, use the decoded image dimensions
  if (current_width <= 0 || current_height <= 0) {
    current_width = width;
    current_height = height;
  }
  
  this->x_scale_ = static_cast<double>(current_width) / width;
  this->y_scale_ = static_cast<double>(current_height) / height;
  
  return true;
}

void ImageDecoder::draw(int x, int y, int w, int h, const Color &color) {
  // Get the current image dimensions (similar to buffer_width_/buffer_height_)
  int buffer_width = this->image_->get_current_width();
  int buffer_height = this->image_->get_current_height();
  
  // Calculate the actual drawing bounds (following original logic)
  auto width = std::min(buffer_width, static_cast<int>(std::ceil((x + w) * this->x_scale_)));
  auto height = std::min(buffer_height, static_cast<int>(std::ceil((y + h) * this->y_scale_)));
  
  // Draw pixels using the available method (similar to draw_pixel_)
  for (int i = static_cast<int>(x * this->x_scale_); i < width; i++) {
    for (int j = static_cast<int>(y * this->y_scale_); j < height; j++) {
      // Use jpeg_decode_pixel which is the closest equivalent to draw_pixel_
      this->image_->jpeg_decode_pixel(i, j, color.r, color.g, color.b);
    }
  }
}

DownloadBuffer::DownloadBuffer(size_t size) : size_(size) {
  this->buffer_ = this->allocator_.allocate(size);
  this->reset();
  if (!this->buffer_) {
    ESP_LOGE(TAG, "Initial allocation of download buffer failed!");
    this->size_ = 0;
  }
}

uint8_t *DownloadBuffer::data(size_t offset) {
  if (offset > this->size_) {
    ESP_LOGE(TAG, "Tried to access beyond download buffer bounds!!!");
    return this->buffer_;
  }
  return this->buffer_ + offset;
}

size_t DownloadBuffer::read(size_t len) {
  this->unread_ -= len;
  if (this->unread_ > 0) {
    memmove(this->data(), this->data(len), this->unread_);
  }
  return this->unread_;
}

size_t DownloadBuffer::resize(size_t size) {
  if (this->size_ >= size) {
    // Avoid useless reallocations; if the buffer is big enough, don't reallocate.
    return this->size_;
  }
  this->allocator_.deallocate(this->buffer_, this->size_);
  this->buffer_ = this->allocator_.allocate(size);
  this->reset();
  if (this->buffer_) {
    this->size_ = size;
    return size;
  } else {
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", size,
             this->allocator_.get_max_free_block_size());
    this->size_ = 0;
    return 0;
  }
}

}  // namespace storage
}  // namespace esphome
