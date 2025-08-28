#include "image_decoder.h"
#include "storage.h"
#include "esphome/core/log.h"

namespace esphome {
namespace storage {

static const char *const TAG = "png_image.decoder";

bool ImageDecoder::set_size(int width, int height) {
  // Set default scale factors
  this->x_scale_ = 1.0;
  this->y_scale_ = 1.0;
  
  // We can't access protected members directly, so we'll use public methods
  // Get current dimensions to calculate scale if needed
  int current_width = this->image_->get_width();
  int current_height = this->image_->get_height();
  
  if (current_width > 0 && current_height > 0 && 
      (current_width != width || current_height != height)) {
    this->x_scale_ = static_cast<double>(current_width) / width;
    this->y_scale_ = static_cast<double>(current_height) / height;
  }
  
  return true;
}

void ImageDecoder::draw(int x, int y, int w, int h, const Color &color) {
  // Calculate scaled coordinates
  int scaled_width = static_cast<int>(std::ceil(w * this->x_scale_));
  int scaled_height = static_cast<int>(std::ceil(h * this->y_scale_));
  int scaled_x = static_cast<int>(x * this->x_scale_);
  int scaled_y = static_cast<int>(y * this->y_scale_);
  
  // Set each pixel in the rectangle
  for (int j = 0; j < scaled_height; j++) {
    for (int i = 0; i < scaled_width; i++) {
      int pixel_x = scaled_x + i;
      int pixel_y = scaled_y + j;
      
      // Use the public wrapper method instead of private set_pixel
      this->image_->set_decoder_pixel(pixel_x, pixel_y, color.r, color.g, color.b, 255);
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
