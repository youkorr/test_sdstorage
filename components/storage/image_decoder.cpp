#include "image_decoder.h"
#include "storage.h"
#include "esphome/core/log.h"

namespace esphome {
namespace storage {

static const char *const TAG = "png_image.decoder";

bool ImageDecoder::set_size(int width, int height) {
  // The SdImageComponent doesn't have a resize_ method, so we'll return true
  // and handle sizing within the component itself
  this->x_scale_ = 1.0;
  this->y_scale_ = 1.0;
  
  // If the image component has resize dimensions set, calculate scale factors
  if (this->image_->resize_width_ > 0 && this->image_->resize_height_ > 0) {
    this->x_scale_ = static_cast<double>(this->image_->resize_width_) / width;
    this->y_scale_ = static_cast<double>(this->image_->resize_height_) / height;
  }
  
  return true;
}

void ImageDecoder::draw(int x, int y, int w, int h, const Color &color) {
  // Calculate scaled coordinates
  int scaled_width = static_cast<int>(std::ceil(w * this->x_scale_));
  int scaled_height = static_cast<int>(std::ceil(h * this->y_scale_));
  int scaled_x = static_cast<int>(x * this->x_scale_);
  int scaled_y = static_cast<int>(y * this->y_scale_);
  
  // For each pixel in the rectangle, set it using the component's buffer directly
  // Since we can't access the private set_pixel method, we'll need to work around this
  
  // Get the image buffer and set pixels directly
  // This requires the buffer to be accessible - you may need to add a friend declaration
  // or make ImageDecoder a friend of SdImageComponent
  
  // For now, just log the operation
  ESP_LOGD(TAG, "Setting rectangle %d,%d %dx%d to color R:%d G:%d B:%d", 
           scaled_x, scaled_y, scaled_width, scaled_height, color.r, color.g, color.b);
  
  // TODO: Implement direct buffer access or add public setter method to SdImageComponent
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
