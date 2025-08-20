#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstring>
#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/optional.h"
#include "esphome/components/image/image.h"
#include "esphome/components/display/display.h"
#include "../sd_mmc_card/sd_mmc_card.h"

// Image decoder configuration for ESP-IDF
#ifdef ESP_IDF_VERSION
  // Use JPEGDEC library which works with ESP-IDF
  #define USE_JPEGDEC
  // Disable PNG for now as it's more complex to get working
  // #define USE_PNGDEC
#else
  // Arduino framework
  #define USE_JPEGDEC
  // #define USE_PNGDEC
#endif

// Image decoders
#ifdef USE_JPEGDEC
#include <JPEGDEC.h>
#endif

#ifdef USE_PNGDEC
#include <PNGDEC.h>
#endif

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;

// Use ESPHome's ImageType enum
using ImageType = image::ImageType;

// Properly declared enums with enum class
enum class OutputImageFormat {
  rgb565,
  rgb888,
  rgba
};

enum class ByteOrder {
  little_endian,
  big_endian
};

// =====================================================
// StorageComponent - Main Storage Class
// =====================================================
class StorageComponent : public Component {
 public:
  StorageComponent() = default;
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration
  void set_platform(const std::string &platform) { this->platform_ = platform; }
  void set_sd_component(sd_mmc_card::SdMmc *sd_component) { this->sd_component_ = sd_component; }
  void set_root_path(const std::string &root_path) { this->root_path_ = root_path; }
  
  // File methods
  bool file_exists_direct(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool write_file_direct(const std::string &path, const std::vector<uint8_t> &data);
  size_t get_file_size(const std::string &path);
  
  // Getters
  const std::string &get_platform() const { return this->platform_; }
  const std::string &get_root_path() const { return this->root_path_; }
  sd_mmc_card::SdMmc *get_sd_component() const { return this->sd_component_; }
  
 private:
  std::string platform_;
  std::string root_path_{"/"}; 
  sd_mmc_card::SdMmc *sd_component_{nullptr};
};

// =====================================================
// SdImageComponent - SD Image Component
// =====================================================
class SdImageComponent : public Component, public image::Image {
 public:
  // Proper constructor for image::Image base class
  SdImageComponent() : image::Image(nullptr, 0, 0, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE) {}

  // Component methods
  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // ===== CONFIGURATION METHODS =====
  void set_file_path(const std::string &path) { this->file_path_ = path; }
  void set_output_format(OutputImageFormat format) { this->output_format_ = format; }
  void set_byte_order(ByteOrder byte_order) { this->byte_order_ = byte_order; }
  void set_storage_component(StorageComponent *storage) { this->storage_component_ = storage; }
  
  // String setter methods (called from Python)
  void set_output_format_string(const std::string &format);
  void set_byte_order_string(const std::string &byte_order);
  
  // Dimension setters
  void set_width(int width) { this->width_ = width; }
  void set_height(int height) { this->height_ = height; }
  void set_resize(int width, int height) { 
    this->width_ = width; 
    this->height_ = height; 
  }
  
  // ===== GETTERS =====
  const std::string &get_file_path() const { return this->file_path_; }
  int get_width() const override { return this->width_; }
  int get_height() const override { return this->height_; }
  OutputImageFormat get_output_format() const { return this->output_format_; }
  ByteOrder get_byte_order() const { return this->byte_order_; }
  bool is_loaded() const { return this->is_loaded_; }
  
  // ===== IMAGE::IMAGE INTERFACE METHODS =====
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  image::ImageType get_image_type() const;
  
  // ===== DATA ACCESS METHODS =====
  const uint8_t *get_data_start() const { 
    return this->image_data_.empty() ? nullptr : this->image_data_.data(); 
  }
  
  size_t get_data_length() const { 
    return this->image_data_.size(); 
  }
  
  // Compatibility methods
  const uint8_t *get_data() const { 
    return this->get_data_start(); 
  }
  size_t get_data_size() const { 
    return this->get_data_length(); 
  }
  
  // ===== IMAGE LOADING/UNLOADING =====
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // ===== PIXEL ACCESS =====
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const;
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const; 
  
  // ===== UTILITY METHODS =====
  bool validate_image_data() const;
  std::string get_output_format_string() const;
  std::string get_byte_order_string() const;
  
  // Display compatibility
  void get_image_dimensions(int *width, int *height) const {
    if (width) *width = this->width_;
    if (height) *height = this->height_;
  }
  
  const uint8_t* get_image_data() const { return this->get_data(); }

  // Utility methods for diagnostics
  bool has_valid_dimensions() const { 
    return this->width_ > 0 && this->height_ > 0; 
  }
  
  std::string get_debug_info() const {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), 
      "SdImage[%s]: %dx%d, %s, loaded=%s, size=%zu bytes",
      this->file_path_.c_str(),
      this->width_, this->height_,
      this->get_output_format_string().c_str(),
      this->is_loaded_ ? "yes" : "no",
      this->image_data_.size()
    );
    return std::string(buffer);
  }

 protected:
  // ===== CONFIGURATION DATA =====
  std::string file_path_;
  int width_{0};
  int height_{0};
  OutputImageFormat output_format_{OutputImageFormat::rgb565};
  ByteOrder byte_order_{ByteOrder::little_endian};
  
  // ===== STATE DATA =====
  bool is_loaded_{false};
  std::vector<uint8_t> image_data_;
  StorageComponent *storage_component_{nullptr};
  
 private:
  // ===== FILE TYPE DETECTION =====
  bool is_jpeg_file(const std::vector<uint8_t> &data) const;
  bool is_png_file(const std::vector<uint8_t> &data) const;
  
  // ===== IMAGE DECODING METHODS =====
  bool decode_jpeg(const std::vector<uint8_t> &jpeg_data);
  bool decode_png(const std::vector<uint8_t> &png_data);
  bool load_raw_data(const std::vector<uint8_t> &raw_data);
  
  // ===== REAL IMAGE DECODERS =====
  bool decode_jpeg_real(const std::vector<uint8_t> &jpeg_data);
  bool decode_png_real(const std::vector<uint8_t> &png_data);
  
  // ===== FALLBACK DECODERS (test patterns) =====
  bool decode_jpeg_fallback(const std::vector<uint8_t> &jpeg_data);
  bool decode_png_fallback(const std::vector<uint8_t> &png_data);
  
  // ===== JPEG DECODING - STACK SAFE IMPLEMENTATION =====
  int jpeg_draw_callback(JPEGDRAW *pDraw);
  std::vector<uint8_t> *jpeg_decode_buffer_{nullptr};
  int jpeg_decode_width_{0};
  int jpeg_decode_height_{0};
  volatile bool jpeg_decode_error_{false};
  
  // ===== DIMENSION EXTRACTION =====
  bool extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  bool extract_png_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  
  // ===== PIXEL MANIPULATION =====
  void convert_pixel_format(int x, int y, const uint8_t *pixel_data, 
                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  size_t get_pixel_size() const;
  size_t get_pixel_offset(int x, int y) const;
  void convert_byte_order();
  size_t calculate_output_size() const;
  
  // ===== FORMAT CONVERSION =====
  void convert_rgb888_to_target(const uint8_t *rgb_data, size_t pixel_count);
  void convert_rgba_to_target(const uint8_t *rgba_data, size_t pixel_count);
  
  // ===== TEST PATTERN GENERATION =====
  void generate_test_pattern(const std::vector<uint8_t> &source_data);
  void generate_jpeg_test_pattern(const std::vector<uint8_t> &source_data);
  void generate_png_test_pattern(const std::vector<uint8_t> &source_data);
  void set_pixel_at_offset(size_t offset, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
  
  // ===== VALIDATION METHODS =====
  bool validate_dimensions() const;
  bool validate_file_path() const;
  bool validate_pixel_access(int x, int y) const;
  
  // ===== UTILITY METHODS =====
  std::string detect_file_type(const std::string &path) const;
  bool is_supported_format(const std::string &extension) const;
  void list_directory_contents(const std::string &dir_path);
  
#ifdef USE_PNGDEC  
  PNG png_decoder_;
#endif
};

// =====================================================
// ACTION CLASSES - SdImageLoadAction
// =====================================================
template<typename... Ts> 
class SdImageLoadAction : public Action<Ts...> {
 public:
  SdImageLoadAction() = default;
  explicit SdImageLoadAction(SdImageComponent *parent) : parent_(parent) {}
  
  // Template value for file path
  TEMPLATABLE_VALUE(std::string, file_path)
  
  void set_parent(SdImageComponent *parent) { this->parent_ = parent; }
  
  void play(Ts... x) override {
    if (this->parent_ == nullptr) {
      ESP_LOGE("sd_image.load", "Parent component is null");
      return;
    }
    
    // If a file path is provided in the action
    if (this->file_path_.has_value()) {
      std::string path = this->file_path_.value(x...);
      if (!path.empty()) {
        ESP_LOGD("sd_image.load", "Loading image from path: %s", path.c_str());
        if (!this->parent_->load_image_from_path(path)) {
          ESP_LOGE("sd_image.load", "Failed to load image from: %s", path.c_str());
        }
        return;
      }
    }
    
    // Otherwise, use the configured path
    ESP_LOGD("sd_image.load", "Loading image from configured path");
    if (!this->parent_->load_image()) {
      ESP_LOGE("sd_image.load", "Failed to load image from configured path");
    }
  }

 private:
  SdImageComponent *parent_{nullptr};
};

// =====================================================
// ACTION CLASSES - SdImageUnloadAction
// =====================================================
template<typename... Ts> 
class SdImageUnloadAction : public Action<Ts...> {
 public:
  SdImageUnloadAction() = default;
  explicit SdImageUnloadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void set_parent(SdImageComponent *parent) { this->parent_ = parent; }
  
  void play(Ts... x) override {
    if (this->parent_ == nullptr) {
      ESP_LOGE("sd_image.unload", "Parent component is null");
      return;
    }
    
    ESP_LOGD("sd_image.unload", "Unloading image: %s", this->parent_->get_debug_info().c_str());
    this->parent_->unload_image();
    ESP_LOGD("sd_image.unload", "Image unloaded successfully");
  }

 private:
  SdImageComponent *parent_{nullptr};
};

}  // namespace storage
}  // namespace esphome






