#include "storage.h"
#include "esphome/core/log.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <algorithm>

namespace esphome {
namespace storage {

static const char *const TAG = "storage";
static const char *const TAG_IMAGE = "storage.image";

// Global decoder instances for callbacks
static SdImageComponent *current_image_component = nullptr;

// =====================================================
// StorageComponent Implementation
// =====================================================

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Root path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  SD component: %s", this->sd_component_ ? "configured" : "not configured");
}

void StorageComponent::loop() {
  // Nothing to do in loop
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Root path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  SD component: %s", this->sd_component_ ? "YES" : "NO");
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  std::string full_path = this->root_path_ + path;
  struct stat st;
  return stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  std::string full_path = this->root_path_ + path;
  FILE *file = fopen(full_path.c_str(), "rb");
  
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file: %s (errno: %d)", full_path.c_str(), errno);
    return {};
  }
  
  // Get file size safely
  if (fseek(file, 0, SEEK_END) != 0) {
    ESP_LOGE(TAG, "Failed to seek to end of file: %s", full_path.c_str());
    fclose(file);
    return {};
  }
  
  long size = ftell(file);
  if (size < 0 || size > 10 * 1024 * 1024) { // 10MB limit
    ESP_LOGE(TAG, "Invalid file size: %ld bytes", size);
    fclose(file);
    return {};
  }
  
  if (fseek(file, 0, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "Failed to seek to beginning of file: %s", full_path.c_str());
    fclose(file);
    return {};
  }
  
  std::vector<uint8_t> data(size);
  size_t read_size = fread(data.data(), 1, size, file);
  fclose(file);
  
  if (read_size != static_cast<size_t>(size)) {
    ESP_LOGE(TAG, "Failed to read complete file: expected %ld, got %zu", size, read_size);
    return {};
  }
  
  return data;
}

bool StorageComponent::write_file_direct(const std::string &path, const std::vector<uint8_t> &data) {
  std::string full_path = this->root_path_ + path;
  FILE *file = fopen(full_path.c_str(), "wb");
  
  if (!file) {
    ESP_LOGE(TAG, "Failed to create file: %s", full_path.c_str());
    return false;
  }
  
  size_t written = fwrite(data.data(), 1, data.size(), file);
  fclose(file);
  
  return written == data.size();
}

size_t StorageComponent::get_file_size(const std::string &path) {
  std::string full_path = this->root_path_ + path;
  struct stat st;
  if (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
    return st.st_size;
  }
  return 0;
}

// =====================================================
// SdImageComponent Implementation  
// =====================================================

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  ESP_LOGCONFIG(TAG_IMAGE, "  File path: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Resize: %dx%d", this->resize_width_, this->resize_height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->format_to_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Auto load: %s", this->auto_load_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG_IMAGE, "  Storage component: %s", this->storage_component_ ? "configured" : "not configured");
  
  // Auto-load image if configured
  if (this->auto_load_ && !this->file_path_.empty() && this->storage_component_) {
    ESP_LOGI(TAG_IMAGE, "Auto-loading image from: %s", this->file_path_.c_str());
    this->load_image();
  }
}

void SdImageComponent::loop() {
  // Nothing to do in loop
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->image_width_, this->image_height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->format_to_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Loaded: %s", this->image_loaded_ ? "YES" : "NO");
  if (this->image_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Buffer size: %zu bytes", this->image_buffer_.size());
  }
}

// Image interface implementation
void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (!this->image_loaded_ || this->image_buffer_.empty()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw: image not loaded");
    return;
  }
  
  // Use ESPHome's image drawing
  for (int img_x = 0; img_x < this->image_width_; img_x++) {
    for (int img_y = 0; img_y < this->image_height_; img_y++) {
      size_t offset = (img_y * this->image_width_ + img_x) * this->get_pixel_size();
      
      if (offset + this->get_pixel_size() <= this->image_buffer_.size()) {
        Color pixel_color;
        
        if (this->format_ == ImageFormat::RGB565) {
          uint16_t rgb565 = this->image_buffer_[offset] | (this->image_buffer_[offset + 1] << 8);
          uint8_t r = (rgb565 >> 11) << 3;
          uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
          uint8_t b = (rgb565 & 0x1F) << 3;
          pixel_color = Color(r, g, b);
        } else if (this->format_ == ImageFormat::RGB888) {
          pixel_color = Color(this->image_buffer_[offset], 
                            this->image_buffer_[offset + 1], 
                            this->image_buffer_[offset + 2]);
        } else if (this->format_ == ImageFormat::RGBA) {
          pixel_color = Color(this->image_buffer_[offset], 
                            this->image_buffer_[offset + 1], 
                            this->image_buffer_[offset + 2], 
                            this->image_buffer_[offset + 3]);
        }
        
        display->draw_pixel_at(x + img_x, y + img_y, pixel_color);
      }
    }
  }
}

int SdImageComponent::get_width() const {
  return this->resize_width_ > 0 ? this->resize_width_ : this->image_width_;
}

int SdImageComponent::get_height() const {
  return this->resize_height_ > 0 ? this->resize_height_ : this->image_height_;
}

ImageType SdImageComponent::get_type() const {
  return this->get_image_type_from_format();
}

// Loading methods
bool SdImageComponent::load_image() {
  return this->load_image_from_path(this->file_path_);
}

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGI(TAG_IMAGE, "Loading image from: %s", path.c_str());
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not available");
    return false;
  }
  
  // Unload previous image
  this->unload_image();
  
  // Check file existence
  if (!this->storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "Image file not found: %s", path.c_str());
    
    // List directory for debugging
    std::string dir_path = path.substr(0, path.find_last_of("/"));
    if (dir_path.empty()) dir_path = "/";
    this->list_directory_contents(dir_path);
    
    return false;
  }
  
  // Read file data
  std::vector<uint8_t> file_data = this->storage_component_->read_file_direct(path);
  if (file_data.empty()) {
    ESP_LOGE(TAG_IMAGE, "Failed to read image file: %s", path.c_str());
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "Read %zu bytes from file", file_data.size());
  
  // Decode image
  if (!this->decode_image(file_data)) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode image: %s", path.c_str());
    return false;
  }
  
  this->file_path_ = path;
  this->image_loaded_ = true;
  
  ESP_LOGI(TAG_IMAGE, "Image loaded successfully: %dx%d, %zu bytes", 
           this->image_width_, this->image_height_, this->image_buffer_.size());
  
  return true;
}

void SdImageComponent::unload_image() {
  this->image_buffer_.clear();
  this->image_buffer_.shrink_to_fit();
  this->image_loaded_ = false;
  this->image_width_ = 0;
  this->image_height_ = 0;
}

bool SdImageComponent::reload_image() {
  std::string path = this->file_path_;
  this->unload_image();
  return this->load_image_from_path(path);
}

// File type detection
SdImageComponent::FileType SdImageComponent::detect_file_type(const std::vector<uint8_t> &data) const {
  if (this->is_jpeg_data(data)) return FileType::JPEG;
  if (this->is_png_data(data)) return FileType::PNG;
  return FileType::UNKNOWN;
}

bool SdImageComponent::is_jpeg_data(const std::vector<uint8_t> &data) const {
  return data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool SdImageComponent::is_png_data(const std::vector<uint8_t> &data) const {
  const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return data.size() >= 8 && std::memcmp(data.data(), png_signature, 8) == 0;
}

// Image decoding
bool SdImageComponent::decode_image(const std::vector<uint8_t> &data) {
  FileType type = this->detect_file_type(data);
  
  switch (type) {
    case FileType::JPEG:
      ESP_LOGI(TAG_IMAGE, "Decoding JPEG image");
      return this->decode_jpeg_image(data);
      
    case FileType::PNG:
      ESP_LOGI(TAG_IMAGE, "Decoding PNG image");
      return this->decode_png_image(data);
      
    default:
      ESP_LOGE(TAG_IMAGE, "Unsupported image format");
      return false;
  }
}

// =====================================================
// JPEG Decoder Implementation (ESPHome style)
// =====================================================

bool SdImageComponent::decode_jpeg_image(const std::vector<uint8_t> &jpeg_data) {
#ifdef USE_JPEGDEC
  ESP_LOGD(TAG_IMAGE, "Using JPEGDEC decoder");
  
  // Set current component for callback
  current_image_component = this;
  
  // Create decoder
  this->jpeg_decoder_ = new JPEGDEC();
  if (!this->jpeg_decoder_) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate JPEG decoder");
    return false;
  }
  
  // Open JPEG data
  int result = this->jpeg_decoder_->openRAM((uint8_t*)jpeg_data.data(), jpeg_data.size(), 
                                           SdImageComponent::jpeg_decode_callback);
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to open JPEG data: %d", result);
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    return false;
  }
  
  // Get image dimensions
  this->image_width_ = this->jpeg_decoder_->getWidth();
  this->image_height_ = this->jpeg_decoder_->getHeight();
  
  ESP_LOGI(TAG_IMAGE, "JPEG dimensions: %dx%d", this->image_width_, this->image_height_);
  
  // Apply resize if specified
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
    ESP_LOGI(TAG_IMAGE, "Resizing to: %dx%d", this->image_width_, this->image_height_);
  }
  
  // Allocate buffer
  if (!this->allocate_image_buffer()) {
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    return false;
  }
  
  // Decode image
  result = this->jpeg_decoder_->decode(0, 0, 0);
  
  // Cleanup
  this->jpeg_decoder_->close();
  delete this->jpeg_decoder_;
  this->jpeg_decoder_ = nullptr;
  current_image_component = nullptr;
  
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode JPEG: %d", result);
    return false;
  }
  
  ESP_LOGD(TAG_IMAGE, "JPEG decoded successfully");
  return true;
  
#else
  ESP_LOGE(TAG_IMAGE, "JPEGDEC not available");
  return false;
#endif
}

#ifdef USE_JPEGDEC
int SdImageComponent::jpeg_decode_callback(JPEGDRAW *draw) {
  if (!current_image_component || !draw) return 0;
  
  // Process each pixel in the draw area
  for (int y = 0; y < draw->iHeight; y++) {
    for (int x = 0; x < draw->iWidth; x++) {
      int pixel_x = draw->x + x;
      int pixel_y = draw->y + y;
      
      // Get RGB values from decode buffer
      int offset = (y * draw->iWidth + x) * 3;
      uint8_t r = draw->pPixels[offset];
      uint8_t g = draw->pPixels[offset + 1];
      uint8_t b = draw->pPixels[offset + 2];
      
      // Set pixel in our buffer
      current_image_component->jpeg_decode_pixel(pixel_x, pixel_y, r, g, b);
    }
  }
  
  return 1; // Continue decoding
}

bool SdImageComponent::jpeg_decode_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  // Apply resize scaling if needed
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    x = (x * this->resize_width_) / this->jpeg_decoder_->getWidth();
    y = (y * this->resize_height_) / this->jpeg_decoder_->getHeight();
  }
  
  // Bounds check
  if (x < 0 || x >= this->image_width_ || y < 0 || y >= this->image_height_) {
    return false;
  }
  
  this->set_pixel(x, y, r, g, b);
  return true;
}
#endif

// =====================================================
// PNG Decoder Implementation (ESPHome style)
// =====================================================

bool SdImageComponent::decode_png_image(const std::vector<uint8_t> &png_data) {
#ifdef USE_PNGDEC
  ESP_LOGD(TAG_IMAGE, "Using PNGDEC decoder");
  
  // Set current component and data source for callbacks
  current_image_component = this;
  this->png_data_source_ = const_cast<std::vector<uint8_t>*>(&png_data);
  this->png_data_position_ = 0;
  
  // Create decoder
  this->png_decoder_ = new PNG();
  if (!this->png_decoder_) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate PNG decoder");
    return false;
  }
  
  // Open PNG data with callbacks
  int result = this->png_decoder_->open((const char*)png_data.data(), png_data.size(),
                                        SdImageComponent::png_open_callback,
                                        SdImageComponent::png_close_callback,
                                        SdImageComponent::png_read_callback,
                                        SdImageComponent::png_seek_callback,
                                        SdImageComponent::png_draw_callback);
  
  if (result != PNG_SUCCESS) {
    ESP_LOGE(TAG_IMAGE, "Failed to open PNG data: %d", result);
    delete this->png_decoder_;
    this->png_decoder_ = nullptr;
    return false;
  }
  
  // Get image dimensions
  this->image_width_ = this->png_decoder_->getWidth();
  this->image_height_ = this->png_decoder_->getHeight();
  
  ESP_LOGI(TAG_IMAGE, "PNG dimensions: %dx%d", this->image_width_, this->image_height_);
  
  // Apply resize if specified
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
    ESP_LOGI(TAG_IMAGE, "Resizing to: %dx%d", this->image_width_, this->image_height_);
  }
  
  // Allocate buffer
  if (!this->allocate_image_buffer()) {
    delete this->png_decoder_;
    this->png_decoder_ = nullptr;
    return false;
  }
  
  // Decode image
  result = this->png_decoder_->decode(nullptr, 0);
  
  // Cleanup
  this->png_decoder_->close();
  delete this->png_decoder_;
  this->png_decoder_ = nullptr;
  this->png_data_source_ = nullptr;
  current_image_component = nullptr;
  
  if (result != PNG_SUCCESS) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode PNG: %d", result);
    return false;
  }
  
  ESP_LOGD(TAG_IMAGE, "PNG decoded successfully");
  return true;
  
#else
  ESP_LOGE(TAG_IMAGE, "PNGDEC not available");
  return false;
#endif
}

#ifdef USE_PNGDEC
void *SdImageComponent::png_open_callback(const char *filename, int32_t *size) {
  if (current_image_component && current_image_component->png_data_source_) {
    *size = current_image_component->png_data_source_->size();
    return current_image_component->png_data_source_->data();
  }
  return nullptr;
}

void SdImageComponent::png_close_callback(void *handle) {
  // Nothing to do
}

int32_t SdImageComponent::png_read_callback(PNGFILE *page, uint8_t *buffer, int32_t length) {
  if (!current_image_component || !current_image_component->png_data_source_) return 0;
  
  size_t available = current_image_component->png_data_source_->size() - current_image_component->png_data_position_;
  size_t to_read = std::min((size_t)length, available);
  
  if (to_read > 0) {
    std::memcpy(buffer, current_image_component->png_data_source_->data() + current_image_component->png_data_position_, to_read);
    current_image_component->png_data_position_ += to_read;
  }
  
  return to_read;
}

int32_t SdImageComponent::png_seek_callback(PNGFILE *page, int32_t position) {
  if (!current_image_component || !current_image_component->png_data_source_) return 0;
  
  if (position < 0 || position >= (int32_t)current_image_component->png_data_source_->size()) {
    return 0;
  }
  
  current_image_component->png_data_position_ = position;
  return position;
}

void SdImageComponent::png_draw_callback(PNGDRAW *draw) {
  if (!current_image_component || !draw) return;
  
  // Process each pixel in the draw line
  for (int x = 0; x < draw->iWidth; x++) {
    int pixel_x = x;
    int pixel_y = draw->y;
    
    // Get RGBA values
    uint8_t *pixel = &draw->pPixels[x * (draw->iHasAlpha ? 4 : 3)];
    uint8_t r = pixel[0];
    uint8_t g = pixel[1];
    uint8_t b = pixel[2];
    uint8_t a = draw->iHasAlpha ? pixel[3] : 255;
    
    // Set pixel in our buffer
    current_image_component->png_decode_pixel(pixel_x, pixel_y, r, g, b, a);
  }
}

bool SdImageComponent::png_decode_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  // Apply resize scaling if needed
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    x = (x * this->resize_width_) / this->png_decoder_->getWidth();
    y = (y * this->resize_height_) / this->png_decoder_->getHeight();
  }
  
  // Bounds check
  if (x < 0 || x >= this->image_width_ || y < 0 || y >= this->image_height_) {
    return false;
  }
  
  this->set_pixel(x, y, r, g, b, a);
  return true;
}
#endif

// =====================================================
// Helper Methods
// =====================================================

bool SdImageComponent::allocate_image_buffer() {
  size_t buffer_size = this->get_buffer_size();
  
  if (buffer_size == 0 || buffer_size > 5 * 1024 * 1024) { // 5MB limit
    ESP_LOGE(TAG_IMAGE, "Invalid buffer size: %zu", buffer_size);
    return false;
  }
  
  try {
    this->image_buffer_.clear();
    this->image_buffer_.resize(buffer_size, 0);
    ESP_LOGD(TAG_IMAGE, "Allocated image buffer: %zu bytes", buffer_size);
    return true;
  } catch (const std::exception& e) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate buffer: %zu bytes", buffer_size);
    return false;
  }
}

void SdImageComponent::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (x < 0 || x >= this->image_width_ || y < 0 || y >= this->image_height_) {
    return;
  }
  
  size_t offset = (y * this->image_width_ + x) * this->get_pixel_size();
  
  if (offset + this->get_pixel_size() > this->image_buffer_.size()) {
    return;
  }
  
  switch (this->format_) {
    case ImageFormat::RGB565: {
      uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      this->image_buffer_[offset] = rgb565 & 0xFF;
      this->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
      break;
    }
    case ImageFormat::RGB888:
      this->image_buffer_[offset] = r;
      this->image_buffer_[offset + 1] = g;
      this->image_buffer_[offset + 2] = b;
      break;
    case ImageFormat::RGBA:
      this->image_buffer_[offset] = r;
      this->image_buffer_[offset + 1] = g;
      this->image_buffer_[offset + 2] = b;
      this->image_buffer_[offset + 3] = a;
      break;
  }
}

size_t SdImageComponent::get_pixel_size() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return 2;
    case ImageFormat::RGB888: return 3;
    case ImageFormat::RGBA: return 4;
    default: return 2;
  }
}

size_t SdImageComponent::get_buffer_size() const {
  return this->image_width_ * this->image_height_ * this->get_pixel_size();
}

ImageType SdImageComponent::get_image_type_from_format() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return image::IMAGE_TYPE_RGB565;
    case ImageFormat::RGB888: return image::IMAGE_TYPE_RGB24;
    case ImageFormat::RGBA: return image::IMAGE_TYPE_RGBA;
    default: return image::IMAGE_TYPE_RGB565;
  }
}

std::string SdImageComponent::format_to_string() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return "RGB565";
    case ImageFormat::RGB888: return "RGB888";
    case ImageFormat::RGBA: return "RGBA";
    default: return "Unknown";
  }
}

std::string SdImageComponent::get_debug_info() const {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), 
    "SdImage[%s]: %dx%d, %s, loaded=%s, size=%zu bytes",
    this->file_path_.c_str(),
    this->image_width_, this->image_height_,
    this->format_to_string().c_str(),
    this->image_loaded_ ? "yes" : "no",
    this->image_buffer_.size()
  );
  return std::string(buffer);
}

void SdImageComponent::list_directory_contents(const std::string &dir_path) {
  ESP_LOGI(TAG_IMAGE, "Directory listing for: %s", dir_path.c_str());
  
  DIR *dir = opendir(dir_path.c_str());
  if (!dir) {
    ESP_LOGE(TAG_IMAGE, "Cannot open directory: %s (errno: %d)", dir_path.c_str(), errno);
    return;
  }
  
  struct dirent *entry;
  int file_count = 0;
  
  while ((entry = readdir(dir)) != nullptr) {
    std::string full_path = dir_path;
    if (full_path.back() != '/') full_path += "/";
    full_path += entry->d_name;
    
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if (S_ISREG(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "  📄 %s (%ld bytes)", entry->d_name, (long)st.st_size);
        file_count++;
      } else if (S_ISDIR(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "  📁 %s/", entry->d_name);
      }
    }
  }
  
  closedir(dir);
  ESP_LOGI(TAG_IMAGE, "Total files: %d", file_count);
}

bool SdImageComponent::extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const {
  for (size_t i = 0; i < data.size() - 10; i++) {
    if (data[i] == 0xFF) {
      uint8_t marker = data[i + 1];
      if (marker >= 0xC0 && marker <= 0xC3) {
        if (i + 9 < data.size()) {
          height = (data[i + 5] << 8) | data[i + 6];
          width = (data[i + 7] << 8) | data[i + 8];
          return true;
        }
      }
    }
  }
  return false;
}

bool SdImageComponent::extract_png_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const {
  if (data.size() >= 24 && data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
    width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    return true;
  }
  return false;
}

}  // namespace storage
}  // namespace esphome







