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
  ESP_LOGCONFIG(TAG_IMAGE, "  Decoders: JPEG available");
  
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

// Compatibility methods for YAML configuration
void SdImageComponent::set_output_format_string(const std::string &format) {
  if (format == "RGB565") {
    this->format_ = ImageFormat::RGB565;
  } else if (format == "RGB888") {
    this->format_ = ImageFormat::RGB888;
  } else if (format == "RGBA") {
    this->format_ = ImageFormat::RGBA;
  } else {
    ESP_LOGW(TAG_IMAGE, "Unknown format: %s, using RGB565", format.c_str());
    this->format_ = ImageFormat::RGB565;
  }
}

void SdImageComponent::set_byte_order_string(const std::string &byte_order) {
  // For now, we just log the byte order setting
  ESP_LOGD(TAG_IMAGE, "Byte order set to: %s", byte_order.c_str());
}

// Image interface implementation
// Remplacer la méthode draw() dans storage.cpp

void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (!this->image_loaded_ || this->image_buffer_.empty()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw: image not loaded");
    return;
  }
  
  ESP_LOGD(TAG_IMAGE, "Drawing image %dx%d at position %d,%d", 
           this->image_width_, this->image_height_, x, y);
  
  // Pour les grands écrans, utiliser une méthode plus efficace
  if (this->image_width_ > 320 || this->image_height_ > 240) {
    // Dessiner par chunks pour éviter les timeouts
    const int CHUNK_HEIGHT = 32;
    
    for (int chunk_y = 0; chunk_y < this->image_height_; chunk_y += CHUNK_HEIGHT) {
      int chunk_height = std::min(CHUNK_HEIGHT, this->image_height_ - chunk_y);
      
      for (int chunk_x = 0; chunk_x < this->image_width_; chunk_x += 64) {
        int chunk_width = std::min(64, this->image_width_ - chunk_x);
        
        // Dessiner le chunk
        for (int cy = 0; cy < chunk_height; cy++) {
          for (int cx = 0; cx < chunk_width; cx++) {
            int img_x = chunk_x + cx;
            int img_y = chunk_y + cy;
            
            this->draw_pixel_at(display, x + img_x, y + img_y, img_x, img_y);
          }
        }
        
        // Yield pour éviter le watchdog timeout
        yield();
      }
    }
  } else {
    // Méthode standard pour les petites images
    for (int img_y = 0; img_y < this->image_height_; img_y++) {
      for (int img_x = 0; img_x < this->image_width_; img_x++) {
        this->draw_pixel_at(display, x + img_x, y + img_y, img_x, img_y);
      }
      
      if (img_y % 16 == 0) yield(); // Yield périodique
    }
  }
  
  ESP_LOGD(TAG_IMAGE, "Image drawing completed");
}

// Nouvelle méthode helper pour dessiner un pixel
void SdImageComponent::draw_pixel_at(display::Display *display, int screen_x, int screen_y, int img_x, int img_y) {
  size_t offset = (img_y * this->image_width_ + img_x) * this->get_pixel_size();
  
  if (offset + this->get_pixel_size() > this->image_buffer_.size()) {
    return;
  }
  
  Color pixel_color;
  
  switch (this->format_) {
    case ImageFormat::RGB565: {
      uint16_t rgb565 = this->image_buffer_[offset] | (this->image_buffer_[offset + 1] << 8);
      uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (rgb565 & 0x1F) << 3;
      pixel_color = Color(r, g, b);
      break;
    }
    case ImageFormat::RGB888:
      pixel_color = Color(this->image_buffer_[offset], 
                        this->image_buffer_[offset + 1], 
                        this->image_buffer_[offset + 2]);
      break;
    case ImageFormat::RGBA:
      pixel_color = Color(this->image_buffer_[offset], 
                        this->image_buffer_[offset + 1], 
                        this->image_buffer_[offset + 2], 
                        this->image_buffer_[offset + 3]);
      break;
    default:
      return;
  }
  
  display->draw_pixel_at(screen_x, screen_y, pixel_color);
}


int SdImageComponent::get_width() const {
  return this->resize_width_ > 0 ? this->resize_width_ : this->image_width_;
}

int SdImageComponent::get_height() const {
  return this->resize_height_ > 0 ? this->resize_height_ : this->image_height_;
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
  
  // Show first few bytes for debugging
  if (file_data.size() >= 16) {
    ESP_LOGI(TAG_IMAGE, "First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
             file_data[0], file_data[1], file_data[2], file_data[3],
             file_data[4], file_data[5], file_data[6], file_data[7],
             file_data[8], file_data[9], file_data[10], file_data[11],
             file_data[12], file_data[13], file_data[14], file_data[15]);
  }
  
  // Decode image
  if (!this->decode_image(file_data)) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode image: %s", path.c_str());
    return false;
  }
  
  this->file_path_ = path;
  this->image_loaded_ = true;
  
  // Update the base Image class properties
  this->update_image_properties();
  
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
  
  // Update base Image class
  this->update_image_properties();
}

bool SdImageComponent::reload_image() {
  std::string path = this->file_path_;
  this->unload_image();
  return this->load_image_from_path(path);
}

void SdImageComponent::update_image_properties() {
  // Update the base Image class properties through inheritance
  // We don't need to access private members, the base class will handle it
  
  // The draw method and get_data_start will provide the data access
  // when needed by the display system
}

// File type detection
SdImageComponent::FileType SdImageComponent::detect_file_type(const std::vector<uint8_t> &data) const {
  if (this->is_jpeg_data(data)) return FileType::JPEG;
  return FileType::UNKNOWN;
}

bool SdImageComponent::is_jpeg_data(const std::vector<uint8_t> &data) const {
  return data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

// Image decoding
bool SdImageComponent::decode_image(const std::vector<uint8_t> &data) {
  FileType type = this->detect_file_type(data);
  
  switch (type) {
    case FileType::JPEG:
      ESP_LOGI(TAG_IMAGE, "Decoding JPEG image");
      return this->decode_jpeg_image(data);
      
    default:
      ESP_LOGE(TAG_IMAGE, "Unsupported image format (only JPEG supported in this build)");
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
    current_image_component = nullptr;
    return false;
  }
  
  // Open JPEG data
  int result = this->jpeg_decoder_->openRAM((uint8_t*)jpeg_data.data(), jpeg_data.size(), 
                                           SdImageComponent::jpeg_decode_callback);
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to open JPEG data: %d", result);
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Get image dimensions
  this->image_width_ = this->jpeg_decoder_->getWidth();
  this->image_height_ = this->jpeg_decoder_->getHeight();
  
  ESP_LOGI(TAG_IMAGE, "JPEG dimensions: %dx%d", this->image_width_, this->image_height_);
  
  // Validate dimensions
  if (this->image_width_ <= 0 || this->image_height_ <= 0 || 
      this->image_width_ > 2048 || this->image_height_ > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid JPEG dimensions: %dx%d", this->image_width_, this->image_height_);
    this->jpeg_decoder_->close();
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Apply resize if specified
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
    ESP_LOGI(TAG_IMAGE, "Resizing to: %dx%d", this->image_width_, this->image_height_);
  }
  
  // Allocate buffer
  if (!this->allocate_image_buffer()) {
    this->jpeg_decoder_->close();
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "Starting JPEG decode...");
  
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
  
  ESP_LOGI(TAG_IMAGE, "JPEG decoded successfully");
  return true;
  
#else
  ESP_LOGE(TAG_IMAGE, "JPEGDEC not available");
  return false;
#endif
}

#ifdef USE_JPEGDEC
int SdImageComponent::jpeg_decode_callback(JPEGDRAW *draw) {
  if (!current_image_component || !draw) {
    ESP_LOGE(TAG_IMAGE, "Invalid callback state");
    return 0;
  }
  
  // Process each pixel in the draw area safely
  for (int y = 0; y < draw->iHeight; y++) {
    for (int x = 0; x < draw->iWidth; x++) {
      int pixel_x = draw->x + x;
      int pixel_y = draw->y + y;
      
      // Bounds check
      if (pixel_x < 0 || pixel_y < 0) continue;
      
      // Get RGB values from decode buffer
      int offset = (y * draw->iWidth + x) * 3;
      if (offset + 2 >= draw->iWidth * draw->iHeight * 3) continue;
      
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
    int orig_width = this->jpeg_decoder_->getWidth();
    int orig_height = this->jpeg_decoder_->getHeight();
    x = (x * this->resize_width_) / orig_width;
    y = (y * this->resize_height_) / orig_height;
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
// Helper Methods
// =====================================================

bool SdImageComponent::allocate_image_buffer() {
  size_t buffer_size = this->get_buffer_size();
  
  if (buffer_size == 0 || buffer_size > 3 * 1024 * 1024) { // 3MB limit for ESP32P4
    ESP_LOGE(TAG_IMAGE, "Invalid buffer size: %zu bytes", buffer_size);
    return false;
  }
  
  this->image_buffer_.clear();
  this->image_buffer_.resize(buffer_size, 0);
  ESP_LOGD(TAG_IMAGE, "Allocated image buffer: %zu bytes", buffer_size);
  return true;
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

image::ImageType SdImageComponent::get_image_type_from_format() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return image::IMAGE_TYPE_RGB565;
    case ImageFormat::RGB888: return image::IMAGE_TYPE_RGB;  // Use RGB instead of RGB24
    case ImageFormat::RGBA: return image::IMAGE_TYPE_RGB;    // Use RGB instead of RGBA
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

}  // namespace storage
}  // namespace esphome










