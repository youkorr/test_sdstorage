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
  // Nothing to do in loop for now
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
    ESP_LOGE(TAG, "Failed to open file: %s", full_path.c_str());
    return {};
  }
  
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  std::vector<uint8_t> data(size);
  size_t read_size = fread(data.data(), 1, size, file);
  fclose(file);
  
  if (read_size != size) {
    ESP_LOGE(TAG, "Failed to read complete file: expected %zu, got %zu", size, read_size);
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
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->get_output_format_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Byte order: %s", this->get_byte_order_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Storage component: %s", this->storage_component_ ? "configured" : "not configured");
  
  // Auto-load image if path is specified
  if (!this->file_path_.empty() && this->storage_component_) {
    ESP_LOGI(TAG_IMAGE, "Auto-loading image from: %s", this->file_path_.c_str());
    this->load_image();
  }
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->get_output_format_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Loaded: %s", this->is_loaded_ ? "YES" : "NO");
  if (this->is_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Data size: %zu bytes", this->image_data_.size());
  }
}

// String setter methods
void SdImageComponent::set_output_format_string(const std::string &format) {
  if (format == "RGB565") {
    this->output_format_ = OutputImageFormat::rgb565;
  } else if (format == "RGB888") {
    this->output_format_ = OutputImageFormat::rgb888;
  } else if (format == "RGBA") {
    this->output_format_ = OutputImageFormat::rgba;
  } else {
    ESP_LOGW(TAG_IMAGE, "Unknown format: %s, using RGB565", format.c_str());
    this->output_format_ = OutputImageFormat::rgb565;
  }
}

void SdImageComponent::set_byte_order_string(const std::string &byte_order) {
  if (byte_order == "LITTLE_ENDIAN") {
    this->byte_order_ = ByteOrder::little_endian;
  } else if (byte_order == "BIG_ENDIAN") {
    this->byte_order_ = ByteOrder::big_endian;
  } else {
    ESP_LOGW(TAG_IMAGE, "Unknown byte order: %s, using LITTLE_ENDIAN", byte_order.c_str());
    this->byte_order_ = ByteOrder::little_endian;
  }
}

// Image type and format methods
// Image type and format methods
image::ImageType SdImageComponent::get_image_type() const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565:
      return image::IMAGE_TYPE_RGB565;
    case OutputImageFormat::rgb888:
      return image::IMAGE_TYPE_RGB;       // ✅ Utilise RGB générique
    case OutputImageFormat::rgba:
      return image::IMAGE_TYPE_RGB;       // ✅ Utilise RGB générique pour RGBA aussi
    default:
      return image::IMAGE_TYPE_RGB565;
  }
}
}

std::string SdImageComponent::get_output_format_string() const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565: return "RGB565";
    case OutputImageFormat::rgb888: return "RGB888";
    case OutputImageFormat::rgba: return "RGBA";
    default: return "Unknown";
  }
}

std::string SdImageComponent::get_byte_order_string() const {
  switch (this->byte_order_) {
    case ByteOrder::little_endian: return "LITTLE_ENDIAN";
    case ByteOrder::big_endian: return "BIG_ENDIAN";
    default: return "Unknown";
  }
}

// File type detection methods
bool SdImageComponent::is_jpeg_file(const std::vector<uint8_t> &data) const {
  return data.size() >= 4 && 
         data[0] == 0xFF && data[1] == 0xD8 && 
         data[2] == 0xFF;
}

bool SdImageComponent::is_png_file(const std::vector<uint8_t> &data) const {
  const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return data.size() >= 8 && 
         std::memcmp(data.data(), png_signature, 8) == 0;
}

// Pixel manipulation methods
size_t SdImageComponent::get_pixel_size() const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565: return 2;
    case OutputImageFormat::rgb888: return 3;
    case OutputImageFormat::rgba: return 4;
    default: return 2;
  }
}

size_t SdImageComponent::get_pixel_offset(int x, int y) const {
  return (y * this->width_ + x) * this->get_pixel_size();
}

size_t SdImageComponent::calculate_output_size() const {
  return this->width_ * this->height_ * this->get_pixel_size();
}

void SdImageComponent::set_pixel_at_offset(size_t offset, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (offset + this->get_pixel_size() > this->image_data_.size()) {
    return;
  }
  
  switch (this->output_format_) {
    case OutputImageFormat::rgb565: {
      uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      if (this->byte_order_ == ByteOrder::little_endian) {
        this->image_data_[offset + 0] = rgb565 & 0xFF;
        this->image_data_[offset + 1] = (rgb565 >> 8) & 0xFF;
      } else {
        this->image_data_[offset + 0] = (rgb565 >> 8) & 0xFF;
        this->image_data_[offset + 1] = rgb565 & 0xFF;
      }
      break;
    }
    case OutputImageFormat::rgb888:
      this->image_data_[offset + 0] = r;
      this->image_data_[offset + 1] = g;
      this->image_data_[offset + 2] = b;
      break;
    case OutputImageFormat::rgba:
      this->image_data_[offset + 0] = r;
      this->image_data_[offset + 1] = g;
      this->image_data_[offset + 2] = b;
      this->image_data_[offset + 3] = a;
      break;
  }
}

// Utility methods
void SdImageComponent::list_directory_contents(const std::string &dir_path) {
  ESP_LOGI(TAG_IMAGE, "📁 Directory listing for: '%s'", dir_path.c_str());
  
  DIR *dir = opendir(dir_path.c_str());
  if (!dir) {
    ESP_LOGE(TAG_IMAGE, "❌ Cannot open directory: '%s' (errno: %d)", dir_path.c_str(), errno);
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
        ESP_LOGI(TAG_IMAGE, "   📄 %s (%ld bytes)", entry->d_name, (long)st.st_size);
        file_count++;
      } else if (S_ISDIR(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "   📁 %s/", entry->d_name);
      }
    } else {
      ESP_LOGI(TAG_IMAGE, "   ❓ %s (stat failed)", entry->d_name);
    }
  }
  
  closedir(dir);
  ESP_LOGI(TAG_IMAGE, "📊 Total files found: %d", file_count);
}

// =====================================================
// TON CODE D'IMAGE LOADING (copie exacte)
// =====================================================

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGI(TAG_IMAGE, "=== IMAGE LOADING START ===");
  ESP_LOGI(TAG_IMAGE, "🔄 Loading image from: '%s'", path.c_str());
  ESP_LOGI(TAG_IMAGE, "Storage component available: %s", this->storage_component_ ? "YES" : "NO");
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "❌ Storage component not available");
    return false;
  }
  
  // Free previous image if loaded
  if (this->is_loaded_) {
    ESP_LOGI(TAG_IMAGE, "🗑️ Unloading previous image");
    this->unload_image();
  }
  
  // Check if file exists with detailed info
  ESP_LOGI(TAG_IMAGE, "🔍 Checking file existence: '%s'", path.c_str());
  if (!this->storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "❌ Image file not found: '%s'", path.c_str());
    
    // List files in directory for debug
    std::string dir_path = path.substr(0, path.find_last_of("/"));
    if (dir_path.empty()) dir_path = "/";
    ESP_LOGI(TAG_IMAGE, "📁 Listing files in directory: '%s'", dir_path.c_str());
    this->list_directory_contents(dir_path);
    
    return false;
  }
  
  // Get file size
  size_t file_size = this->storage_component_->get_file_size(path);
  ESP_LOGI(TAG_IMAGE, "📏 File size: %zu bytes", file_size);
  
  if (file_size == 0) {
    ESP_LOGE(TAG_IMAGE, "❌ File is empty: '%s'", path.c_str());
    return false;
  }
  
  if (file_size > 2 * 1024 * 1024) { // 2MB limit
    ESP_LOGW(TAG_IMAGE, "⚠️ Large file detected: %zu bytes (>2MB)", file_size);
  }
  
  // Read data from SD
  ESP_LOGI(TAG_IMAGE, "📖 Reading file data...");
  std::vector<uint8_t> file_data = this->storage_component_->read_file_direct(path);
  
  if (file_data.empty()) {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to read image file data");
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "✅ Successfully read %zu bytes from file", file_data.size());
  
  // Show first few bytes for debugging
  ESP_LOGI(TAG_IMAGE, "🔍 First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
           file_data[0], file_data[1], file_data[2], file_data[3],
           file_data[4], file_data[5], file_data[6], file_data[7],
           file_data[8], file_data[9], file_data[10], file_data[11],
           file_data[12], file_data[13], file_data[14], file_data[15]);
  
  // Detect file type with detailed logging
  bool decode_success = false;
  std::string detected_type = "unknown";
  
  if (this->is_jpeg_file(file_data)) {
    detected_type = "JPEG";
    ESP_LOGI(TAG_IMAGE, "🖼️ Detected file type: JPEG");
    ESP_LOGI(TAG_IMAGE, "🔄 Starting JPEG decoding...");
    decode_success = this->decode_jpeg(file_data);
  } else if (this->is_png_file(file_data)) {
    detected_type = "PNG";
    ESP_LOGI(TAG_IMAGE, "🖼️ Detected file type: PNG");
    ESP_LOGI(TAG_IMAGE, "🔄 Starting PNG decoding...");
    decode_success = this->decode_png(file_data);
  } else {
    detected_type = "RAW/Unknown";
    ESP_LOGI(TAG_IMAGE, "🖼️ Unknown file type, assuming raw bitmap data");
    
    // For raw data, we need dimensions set in advance
    if (this->width_ <= 0 || this->height_ <= 0) {
      ESP_LOGE(TAG_IMAGE, "❌ Dimensions must be set for raw data. Current: %dx%d", 
               this->width_, this->height_);
      ESP_LOGE(TAG_IMAGE, "💡 Set dimensions first: my_image.set_dimensions(width, height)");
      return false;
    }
    ESP_LOGI(TAG_IMAGE, "🔄 Processing as raw data with dimensions: %dx%d", this->width_, this->height_);
    decode_success = this->load_raw_data(file_data);
  }
  
  if (!decode_success) {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to decode %s image: '%s'", detected_type.c_str(), path.c_str());
    return false;
  }
  
  // Final validation
  if (this->image_data_.empty()) {
    ESP_LOGE(TAG_IMAGE, "❌ No image data after decoding");
    return false;
  }
  
  if (this->width_ <= 0 || this->height_ <= 0) {
    ESP_LOGE(TAG_IMAGE, "❌ Invalid dimensions after decoding: %dx%d", this->width_, this->height_);
    return false;
  }
  
  // Update current path and mark as loaded
  this->file_path_ = path;
  this->is_loaded_ = true;
  
  // Success summary
  ESP_LOGI(TAG_IMAGE, "🎉 IMAGE LOADED SUCCESSFULLY!");
  ESP_LOGI(TAG_IMAGE, "   Type: %s", detected_type.c_str());
  ESP_LOGI(TAG_IMAGE, "   Dimensions: %dx%d pixels", this->width_, this->height_);
  ESP_LOGI(TAG_IMAGE, "   Format: %s", this->get_output_format_string().c_str());
  ESP_LOGI(TAG_IMAGE, "   Data size: %zu bytes", this->image_data_.size());
  ESP_LOGI(TAG_IMAGE, "   Memory usage: %.1f KB", this->image_data_.size() / 1024.0f);
  ESP_LOGI(TAG_IMAGE, "=== IMAGE LOADING SUCCESS ===");
  
  return true;
}

bool SdImageComponent::decode_jpeg(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "=== JPEG DECODER START ===");
  ESP_LOGI(TAG_IMAGE, "📊 Processing JPEG file: %zu bytes", jpeg_data.size());
  
  // Verify JPEG signature
  if (!this->is_jpeg_file(jpeg_data)) {
    ESP_LOGE(TAG_IMAGE, "❌ Invalid JPEG signature");
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "✅ JPEG signature verified");
  
  // Extract dimensions from JPEG header with detailed logging
  int detected_width = 0, detected_height = 0;
  ESP_LOGI(TAG_IMAGE, "🔍 Extracting JPEG dimensions...");
  
  if (!this->extract_jpeg_dimensions(jpeg_data, detected_width, detected_height)) {
    ESP_LOGW(TAG_IMAGE, "⚠️ Could not extract JPEG dimensions from header");
    ESP_LOGI(TAG_IMAGE, "🔍 Searching for SOF markers...");
    
    // More detailed search
    bool found_sof = false;
    for (size_t i = 0; i < jpeg_data.size() - 10; i++) {
      if (jpeg_data[i] == 0xFF) {
        uint8_t marker = jpeg_data[i + 1];
        ESP_LOGV(TAG_IMAGE, "Found marker 0xFF%02X at position %zu", marker, i);
        
        if (marker >= 0xC0 && marker <= 0xC3) {
          ESP_LOGI(TAG_IMAGE, "🎯 Found SOF marker 0xFF%02X at position %zu", marker, i);
          found_sof = true;
        }
      }
    }
    
    if (!found_sof) {
      ESP_LOGW(TAG_IMAGE, "⚠️ No SOF markers found in JPEG");
    }
    
    // Use default dimensions
    detected_width = 320;
    detected_height = 240;
    ESP_LOGI(TAG_IMAGE, "📐 Using default dimensions: %dx%d", detected_width, detected_height);
  } else {
    ESP_LOGI(TAG_IMAGE, "✅ JPEG dimensions extracted: %dx%d", detected_width, detected_height);
  }
  
  // Validate dimensions
  if (detected_width <= 0 || detected_height <= 0 || detected_width > 2048 || detected_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "❌ Invalid JPEG dimensions: %dx%d", detected_width, detected_height);
    return false;
  }
  
  this->width_ = detected_width;
  this->height_ = detected_height;
  
  ESP_LOGI(TAG_IMAGE, "📏 Final dimensions: %dx%d", this->width_, this->height_);
  
  // Calculate output size based on configured format
  size_t output_size = this->calculate_output_size();
  ESP_LOGI(TAG_IMAGE, "💾 Allocating %zu bytes for %s format", 
           output_size, this->get_output_format_string().c_str());
  
  this->image_data_.resize(output_size);
  
  // TODO: IMPLEMENT REAL JPEG DECODER HERE
  // For now, create a recognizable test pattern based on JPEG data
  ESP_LOGW(TAG_IMAGE, "⚠️ Using test pattern - REAL JPEG decoder not implemented yet");
  ESP_LOGI(TAG_IMAGE, "🎨 Generating JPEG-based test pattern...");
  
  this->generate_jpeg_test_pattern(jpeg_data);
  
  ESP_LOGI(TAG_IMAGE, "✅ JPEG processing complete: %dx%d %s (%zu bytes)", 
           this->width_, this->height_, this->get_output_format_string().c_str(), output_size);
  ESP_LOGI(TAG_IMAGE, "=== JPEG DECODER END ===");
  
  return true;
}

bool SdImageComponent::decode_png(const std::vector<uint8_t> &png_data) {
  ESP_LOGI(TAG_IMAGE, "=== PNG DECODER START ===");
  ESP_LOGI(TAG_IMAGE, "📊 Processing PNG file: %zu bytes", png_data.size());
  
  // Verify PNG signature
  if (!this->is_png_file(png_data)) {
    ESP_LOGE(TAG_IMAGE, "❌ Invalid PNG signature");
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "✅ PNG signature verified");
  
  // Extract PNG dimensions from IHDR chunk with detailed logging
  int detected_width = 0, detected_height = 0;
  ESP_LOGI(TAG_IMAGE, "🔍 Extracting PNG dimensions from IHDR chunk...");
  
  if (!this->extract_png_dimensions(png_data, detected_width, detected_height)) {
    ESP_LOGW(TAG_IMAGE, "⚠️ Could not extract PNG dimensions from IHDR");
    
    // Show IHDR chunk info for debugging
    if (png_data.size() >= 33) {
      ESP_LOGI(TAG_IMAGE, "🔍 IHDR chunk info:");
      ESP_LOGI(TAG_IMAGE, "   Length: %02X%02X%02X%02X", png_data[8], png_data[9], png_data[10], png_data[11]);
      ESP_LOGI(TAG_IMAGE, "   Type: %c%c%c%c", png_data[12], png_data[13], png_data[14], png_data[15]);
      ESP_LOGI(TAG_IMAGE, "   Width bytes: %02X%02X%02X%02X", png_data[16], png_data[17], png_data[18], png_data[19]);
      ESP_LOGI(TAG_IMAGE, "   Height bytes: %02X%02X%02X%02X", png_data[20], png_data[21], png_data[22], png_data[23]);
    }
    
    // Use default dimensions
    detected_width = 320;
    detected_height = 240;
    ESP_LOGI(TAG_IMAGE, "📐 Using default dimensions: %dx%d", detected_width, detected_height);
  } else {
    ESP_LOGI(TAG_IMAGE, "✅ PNG dimensions extracted: %dx%d", detected_width, detected_height);
  }
  
  // Validate dimensions
  if (detected_width <= 0 || detected_height <= 0 || detected_width > 2048 || detected_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "❌ Invalid PNG dimensions: %dx%d", detected_width, detected_height);
    return false;
  }
  
  this->width_ = detected_width;
  this->height_ = detected_height;
  
  ESP_LOGI(TAG_IMAGE, "📏 Final dimensions: %dx%d", this->width_, this->height_);
  
  // Create image in configured format
  size_t output_size = this->calculate_output_size();
  ESP_LOGI(TAG_IMAGE, "💾 Allocating %zu bytes for %s format", 
           output_size, this->get_output_format_string().c_str());
  
  this->image_data_.resize(output_size);
  
  // TODO: IMPLEMENT REAL PNG DECODER HERE
  // For now, create a recognizable test pattern based on PNG data
  ESP_LOGW(TAG_IMAGE, "⚠️ Using test pattern - REAL PNG decoder not implemented yet");
  ESP_LOGI(TAG_IMAGE, "🎨 Generating PNG-based test pattern...");
  
  this->generate_png_test_pattern(png_data);
  
  ESP_LOGI(TAG_IMAGE, "✅ PNG processing complete: %dx%d %s (%zu bytes)", 
           this->width_, this->height_, this->get_output_format_string().c_str(), output_size);
  ESP_LOGI(TAG_IMAGE, "=== PNG DECODER END ===");
  
  return true;
}

// Améliorations des méthodes d'extraction de dimensions
bool SdImageComponent::extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const {
  ESP_LOGD(TAG_IMAGE, "🔍 Scanning JPEG for SOF markers...");
  
  for (size_t i = 0; i < data.size() - 10; i++) {
    if (data[i] == 0xFF) {
      uint8_t marker = data[i + 1];
      
      // SOF0, SOF1, SOF2, SOF3 markers
      if (marker >= 0xC0 && marker <= 0xC3) {
        ESP_LOGD(TAG_IMAGE, "🎯 Found SOF%d marker at position %zu", marker - 0xC0, i);
        
        if (i + 9 < data.size()) {
          // Skip marker (2 bytes) + length (2 bytes) + precision (1 byte)
          height = (data[i + 5] << 8) | data[i + 6];
          width = (data[i + 7] << 8) | data[i + 8];
          
          ESP_LOGD(TAG_IMAGE, "✅ Extracted dimensions: %dx%d", width, height);
          return true;
        } else {
          ESP_LOGW(TAG_IMAGE, "⚠️ SOF marker found but not enough data");
        }
      }
    }
  }
  
  ESP_LOGD(TAG_IMAGE, "❌ No valid SOF markers found");
  return false;
}

bool SdImageComponent::extract_png_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const {
  ESP_LOGD(TAG_IMAGE, "🔍 Reading PNG IHDR chunk...");
  
  if (data.size() >= 24) {
    // Check if we have IHDR chunk at expected position
    if (data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
      ESP_LOGD(TAG_IMAGE, "✅ IHDR chunk found at expected position");
      
      // Width: bytes 16-19, Height: bytes 20-23 (big endian)
      width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
      height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
      
      ESP_LOGD(TAG_IMAGE, "✅ Extracted dimensions: %dx%d", width, height);
      return true;
    } else {
      ESP_LOGW(TAG_IMAGE, "⚠️ IHDR chunk not found at expected position");
      ESP_LOGW(TAG_IMAGE, "   Expected: IHDR, Got: %c%c%c%c", 
               data[12], data[13], data[14], data[15]);
    }
  } else {
    ESP_LOGW(TAG_IMAGE, "⚠️ PNG file too small for IHDR chunk: %zu bytes", data.size());
  }
  
  return false;
}

// Nouveaux patterns de test plus reconnaissables
void SdImageComponent::generate_jpeg_test_pattern(const std::vector<uint8_t> &source_data) {
  ESP_LOGI(TAG_IMAGE, "🎨 Generating JPEG test pattern (gradient + hash pattern)");
  
  for (int y = 0; y < this->height_; y++) {
    for (int x = 0; x < this->width_; x++) {
      size_t offset = this->get_pixel_offset(x, y);
      
      // Create a gradient pattern with JPEG-like characteristics
      uint8_t r = (x * 255) / this->width_;                    // Horizontal red gradient
      uint8_t g = (y * 255) / this->height_;                   // Vertical green gradient
      uint8_t b = ((x + y) * 128) / (this->width_ + this->height_); // Diagonal blue gradient
      
      // Add some "compression artifacts" pattern
      if ((x / 8) % 2 == 0 && (y / 8) % 2 == 0) {
        r = std::min(255, (int)r + 50);
      }
      
      this->set_pixel_at_offset(offset, r, g, b, 255);
    }
  }
}

void SdImageComponent::generate_png_test_pattern(const std::vector<uint8_t> &source_data) {
  ESP_LOGI(TAG_IMAGE, "🎨 Generating PNG test pattern (checkboard + borders)");
  
  for (int y = 0; y < this->height_; y++) {
    for (int x = 0; x < this->width_; x++) {
      size_t offset = this->get_pixel_offset(x, y);
      
      uint8_t r = 0, g = 0, b = 0;
      
      // Border
      if (x < 5 || x >= this->width_ - 5 || y < 5 || y >= this->height_ - 5) {
        r = 255; g = 0; b = 0;  // Red border
      }
      // Checkerboard pattern
      else if (((x / 16) + (y / 16)) % 2 == 0) {
        r = 255; g = 255; b = 255;  // White squares
      } else {
        r = 0; g = 0; b = 255;      // Blue squares
      }
      
      this->set_pixel_at_offset(offset, r, g, b, 255);
    }
  }
}

// Méthodes manquantes à ajouter au header
bool SdImageComponent::load_image() {
  return this->load_image_from_path(this->file_path_);
}

void SdImageComponent::unload_image() {
  ESP_LOGI(TAG_IMAGE, "🗑️ Unloading image data");
  this->image_data_.clear();
  this->is_loaded_ = false;
}

bool SdImageComponent::reload_image() {
  this->unload_image();
  return this->load_image();
}

bool SdImageComponent::load_raw_data(const std::vector<uint8_t> &raw_data) {
  ESP_LOGI(TAG_IMAGE, "🔄 Loading raw data: %zu bytes", raw_data.size());
  
  size_t expected_size = this->calculate_output_size();
  
  if (raw_data.size() == expected_size) {
    // Direct copy
    this->image_data_ = raw_data;
    ESP_LOGI(TAG_IMAGE, "✅ Raw data loaded directly");
  } else {
    // Generate pattern if size doesn't match
    ESP_LOGW(TAG_IMAGE, "⚠️ Raw data size mismatch. Expected: %zu, Got: %zu", expected_size, raw_data.size());
    this->image_data_.resize(expected_size);
    this->generate_test_pattern(raw_data);
  }
  
  return true;
}

void SdImageComponent::generate_test_pattern(const std::vector<uint8_t> &source_data) {
  ESP_LOGI(TAG_IMAGE, "🎨 Generating generic test pattern");
  
  for (int y = 0; y < this->height_; y++) {
    for (int x = 0; x < this->width_; x++) {
      size_t offset = this->get_pixel_offset(x, y);
      
      uint8_t r = (x * 255) / this->width_;
      uint8_t g = (y * 255) / this->height_;
      uint8_t b = 128;
      
      this->set_pixel_at_offset(offset, r, g, b, 255);
    }
  }
}

// Draw method (required by Image interface)
void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (!this->is_loaded_ || this->image_data_.empty()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw: image not loaded");
    return;
  }
  
  // TODO: Implement proper drawing
  ESP_LOGD(TAG_IMAGE, "Drawing image at %d,%d", x, y);
}

// Pixel access methods
void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const {
  uint8_t alpha;
  this->get_pixel(x, y, red, green, blue, alpha);
}

void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  if (!this->validate_pixel_access(x, y) || !this->is_loaded_) {
    red = green = blue = alpha = 0;
    return;
  }
  
  size_t offset = this->get_pixel_offset(x, y);
  
  switch (this->output_format_) {
    case OutputImageFormat::rgb565: {
      uint16_t rgb565;
      if (this->byte_order_ == ByteOrder::little_endian) {
        rgb565 = this->image_data_[offset] | (this->image_data_[offset + 1] << 8);
      } else {
        rgb565 = (this->image_data_[offset] << 8) | this->image_data_[offset + 1];
      }
      red = (rgb565 >> 11) << 3;
      green = ((rgb565 >> 5) & 0x3F) << 2;
      blue = (rgb565 & 0x1F) << 3;
      alpha = 255;
      break;
    }
    case OutputImageFormat::rgb888:
      red = this->image_data_[offset + 0];
      green = this->image_data_[offset + 1];
      blue = this->image_data_[offset + 2];
      alpha = 255;
      break;
    case OutputImageFormat::rgba:
      red = this->image_data_[offset + 0];
      green = this->image_data_[offset + 1];
      blue = this->image_data_[offset + 2];
      alpha = this->image_data_[offset + 3];
      break;
  }
}

bool SdImageComponent::validate_pixel_access(int x, int y) const {
  return x >= 0 && x < this->width_ && y >= 0 && y < this->height_;
}

bool SdImageComponent::validate_image_data() const {
  if (!this->is_loaded_ || this->image_data_.empty()) {
    return false;
  }
  
  size_t expected_size = this->calculate_output_size();
  return this->image_data_.size() == expected_size;
}

}  // namespace storage
}  // namespace esphome
