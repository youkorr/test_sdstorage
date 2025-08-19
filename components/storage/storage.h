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

// Include JPEGDEC if available
#ifdef USE_JPEGDEC
#include <JPEGDEC.h>
#endif

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;

// Add this struct declaration BEFORE the SdImageComponent class
struct DecodeContext {
  class SdImageComponent* component;
  std::vector<uint8_t>* buffer;
  int width;
  int height;
  size_t pixels_processed;
  
  DecodeContext(class SdImageComponent* comp, std::vector<uint8_t>* buf, int w, int h) 
    : component(comp), buffer(buf), width(w), height(h), pixels_processed(0) {}
};

// Global context pointer for JPEG decoding
extern DecodeContext* g_decode_context;

// Your existing StorageComponent class stays the same...
class StorageComponent : public Component {
 public:
  void setup() override;
  void loop() override; 
  void dump_config() override;
  
  void set_platform(const std::string &platform) { this->platform_ = platform; }
  void set_root_path(const std::string &root_path) { this->root_path_ = root_path; }
  void set_sd_component(void *sd_component) { this->sd_component_ = sd_component; }
  
  bool file_exists_direct(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool write_file_direct(const std::string &path, const std::vector<uint8_t> &data);
  size_t get_file_size(const std::string &path);
  
 protected:
  std::string platform_;
  std::string root_path_;
  void *sd_component_{nullptr};
};

// Your existing SdImageComponent class with added method declarations
class SdImageComponent : public Component, public image::Image {
 public:
  void setup() override;
  void dump_config() override;
  
  // Configuration methods
  void set_file_path(const std::string &path) { this->file_path_ = path; }
  void set_dimensions(int width, int height) { this->width_ = width; this->height_ = height; }
  void set_storage_component(StorageComponent *storage) { this->storage_component_ = storage; }
  
  // String setters for YAML configuration
  void set_output_format_string(const std::string &format);
  void set_byte_order_string(const std::string &byte_order);
  
  // Image loading methods
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // Image properties
  int get_width() const override { return this->width_; }
  int get_height() const override { return this->height_; }
  image::ImageType get_image_type() const override;
  bool is_loaded() const { return this->is_loaded_; }
  
  // Drawing methods
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  
  // Pixel access methods
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const;
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  
  // ADD THESE NEW METHOD DECLARATIONS:
  bool decode_jpeg_tiled(const std::vector<uint8_t> &jpeg_data);
  bool decode_jpeg_tile(const std::vector<uint8_t> &jpeg_data, int tile_x, int tile_y, int tile_w, int tile_h);

 protected:
  // Existing enums
  enum class OutputImageFormat {
    rgb565,
    rgb888, 
    rgba
  };
  
  enum class ByteOrder {
    little_endian,
    big_endian
  };
  
  // Existing member variables
  std::string file_path_;
  int width_{0};
  int height_{0};
  OutputImageFormat output_format_{OutputImageFormat::rgb565};
  ByteOrder byte_order_{ByteOrder::little_endian};
  StorageComponent *storage_component_{nullptr};
  std::vector<uint8_t> image_data_;
  bool is_loaded_{false};
  
  // Existing utility methods
  std::string get_output_format_string() const;
  std::string get_byte_order_string() const;
  
  // File type detection
  bool is_jpeg_file(const std::vector<uint8_t> &data) const;
  bool is_png_file(const std::vector<uint8_t> &data) const;
  
  // Pixel manipulation
  size_t get_pixel_size() const;
  size_t get_pixel_offset(int x, int y) const;
  size_t calculate_output_size() const;
  void set_pixel_at_offset(size_t offset, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
  
  // Image decoding methods
  bool decode_jpeg(const std::vector<uint8_t> &jpeg_data);
  bool decode_jpeg_real(const std::vector<uint8_t> &jpeg_data);
  bool decode_jpeg_fallback(const std::vector<uint8_t> &jpeg_data);
  bool decode_png(const std::vector<uint8_t> &png_data);
  bool decode_png_real(const std::vector<uint8_t> &png_data);
  bool decode_png_fallback(const std::vector<uint8_t> &png_data);
  
  // Dimension extraction
  bool extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  bool extract_png_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  
  // Pattern generation
  void generate_jpeg_test_pattern(const std::vector<uint8_t> &source_data);
  void generate_png_test_pattern(const std::vector<uint8_t> &source_data);
  void generate_test_pattern(const std::vector<uint8_t> &source_data);
  
  // Raw data handling
  bool load_raw_data(const std::vector<uint8_t> &raw_data);
  
  // Format conversion
  void convert_rgb888_to_target(const uint8_t *rgb_data, size_t pixel_count);
  void convert_rgba_to_target(const uint8_t *rgba_data, size_t pixel_count);
  
  // Validation and utility
  bool validate_pixel_access(int x, int y) const;
  bool validate_image_data() const;
  void list_directory_contents(const std::string &dir_path);
};

}  // namespace storage
}  // namespace esphome





