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
  #define USE_JPEGDEC
  #if defined(CONFIG_ESPHOME_ENABLE_PNGLE) || defined(USE_STORAGE_PNG_SUPPORT)
    #define USE_PNGLE
  #endif
#else
  #define USE_JPEGDEC
  #if defined(ENABLE_PNGLE) || defined(USE_STORAGE_PNG_SUPPORT)
    #define USE_PNGLE
  #endif
#endif

// Image decoders
#ifdef USE_JPEGDEC
#include <JPEGDEC.h>
#endif

#ifdef USE_PNGLE
#include <pngle.h>
#endif

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;
class SdImageComponent;

// Image format enums
enum class ImageFormat {
  RGB565,
  RGB888,
  RGBA
};

enum class SdByteOrder {
  LITTLE_ENDIAN_SD,
  BIG_ENDIAN_SD
};

// =====================================================
// StorageComponent - Main Storage Class AVEC AUTO_LOAD GLOBAL
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
  
  // NOUVEAU: Configuration auto_load global
  void set_auto_load(bool auto_load) { this->auto_load_ = auto_load; }
  bool get_auto_load() const { return this->auto_load_; }
  
  // File methods
  bool file_exists_direct(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool write_file_direct(const std::string &path, const std::vector<uint8_t> &data);
  size_t get_file_size(const std::string &path);
  
  // NOUVEAU: Gestion des images SD enregistrées
  void register_sd_image(SdImageComponent *image) { this->sd_images_.push_back(image); }
  void load_all_images();
  void unload_all_images();
  
  // Getters
  const std::string &get_platform() const { return this->platform_; }
  const std::string &get_root_path() const { return this->root_path_; }
  sd_mmc_card::SdMmc *get_sd_component() const { return this->sd_component_; }
  
 private:
  std::string platform_;
  std::string root_path_{"/"}; 
  sd_mmc_card::SdMmc *sd_component_{nullptr};
  
  // NOUVEAU: Auto-load global et gestion des images
  bool auto_load_{true}; // Par défaut à true pour compatibilité
  std::vector<SdImageComponent*> sd_images_;
};

// =====================================================
// SdImageComponent - SD Card Image Component
// =====================================================
class SdImageComponent : public Component, public image::Image {
 public:
  // Constructor - initializes base Image class with valid data
  SdImageComponent() : Component(), 
                       image::Image(nullptr, 0, 0, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE) {}

  // Component lifecycle
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration setters
  void set_file_path(const std::string &path) { this->file_path_ = path; }
  void set_storage_component(StorageComponent *storage) { 
    this->storage_component_ = storage; 
    // S'enregistrer auprès du composant storage principal
    if (storage) {
      storage->register_sd_image(this);
    }
  }
  void set_resize(int width, int height) { 
    this->resize_width_ = width; 
    this->resize_height_ = height; 
  }
  void set_format(ImageFormat format) { this->format_ = format; }
  
  // Compatibility methods for YAML configuration
  void set_output_format_string(const std::string &format);
  void set_byte_order_string(const std::string &byte_order);
  
  // Override Image methods avec chargement automatique intégré
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  int get_width() const override;
  int get_height() const override;
  
  // Loading/unloading methods
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // NOUVEAU: Méthodes pour système hybride (auto_load global + on-demand)
  bool should_auto_load() const { 
    return this->storage_component_ && this->storage_component_->get_auto_load(); 
  }
  bool ensure_loaded(); // Chargement intelligent selon le mode
  
  // Finalize loading
  void finalize_image_load();
  
  // Status et accès aux données
  bool is_loaded() const { return this->image_loaded_; }
  const std::string &get_file_path() const { return this->file_path_; }
  
  // Image buffer access for LVGL
  const std::vector<uint8_t> &get_image_buffer() const { return this->image_buffer_; }
  uint8_t* get_image_data() { return this->image_buffer_.empty() ? nullptr : this->image_buffer_.data(); }
  size_t get_image_data_size() const { return this->image_buffer_.size(); }
  
  // NOUVEAU: Méthodes pour LVGL avec chargement automatique
  const uint8_t* get_image_data_for_lvgl();
  size_t get_image_data_size_for_lvgl();
  
  // Debug info
  std::string get_debug_info() const;

 protected:
  // Image state
  std::string file_path_;
  StorageComponent *storage_component_{nullptr};
  std::vector<uint8_t> image_buffer_;
  bool image_loaded_{false};
  
  // Image properties - local
  int image_width_{0};
  int image_height_{0};
  int resize_width_{0};
  int resize_height_{0};
  ImageFormat format_{ImageFormat::RGB565};
  SdByteOrder byte_order_{SdByteOrder::LITTLE_ENDIAN_SD};

 private:
  // État de chargement pour système hybride
  enum class LoadState {
    NOT_LOADED,
    LOADING,
    LOADED,
    FAILED
  };
  
  LoadState load_state_{LoadState::NOT_LOADED};
  uint32_t last_load_attempt_{0};
  uint32_t load_retry_count_{0};
  static const uint32_t MAX_LOAD_RETRIES = 3;
  static const uint32_t LOAD_RETRY_DELAY_MS = 1000;
  
  // Retry logic pour auto-loading (legacy)
  bool retry_load_{false};
  uint32_t last_retry_attempt_{0};
  static const uint32_t RETRY_INTERVAL_MS = 2000;
  
  // File type detection
  enum class FileType {
    UNKNOWN,
    JPEG,
    PNG
  };
  
  FileType detect_file_type(const std::vector<uint8_t> &data) const;
  bool is_jpeg_data(const std::vector<uint8_t> &data) const;
  bool is_png_data(const std::vector<uint8_t> &data) const;
  
  // Image decoding
  bool decode_image(const std::vector<uint8_t> &data);
  bool decode_jpeg_image(const std::vector<uint8_t> &jpeg_data);
  bool decode_png_image(const std::vector<uint8_t> &png_data);
  
  // Decoder callbacks and helpers
#ifdef USE_JPEGDEC
  static int jpeg_decode_callback(JPEGDRAW *draw);
  static int jpeg_decode_callback_no_resize(JPEGDRAW *draw);
  JPEGDEC *jpeg_decoder_{nullptr};
  bool jpeg_decode_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
#endif

#ifdef USE_PNGLE
  static void png_init_callback(pngle_t *pngle, uint32_t w, uint32_t h);
  static void png_draw_callback(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]);
  static void png_done_callback(pngle_t *pngle);
  static void png_init_callback_no_resize(pngle_t *pngle, uint32_t w, uint32_t h);
  static void png_draw_callback_no_resize(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]);
  pngle_t *png_decoder_{nullptr};
#endif

  // Image processing
  bool allocate_image_buffer();
  void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
  size_t get_pixel_size() const;
  size_t get_buffer_size() const;
  
  // Resize methods
  bool resize_image_buffer(int src_width, int src_height, int dst_width, int dst_height);
  bool resize_image_buffer_bilinear(int src_width, int src_height, int dst_width, int dst_height);
  
  // Base class property updates
  void update_base_image_properties();
  
  int get_current_width() const;
  int get_current_height() const;
  image::ImageType get_esphome_image_type() const;
  
  void draw_pixels_directly(int x, int y, display::Display *display, Color color_on, Color color_off);
  void draw_pixel_at(display::Display *display, int screen_x, int screen_y, int img_x, int img_y);
  Color get_pixel_color(int x, int y) const;
  
  // Utility methods
  void list_directory_contents(const std::string &dir_path);
  bool extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  
  // Format helpers
  std::string format_to_string() const;
};

// =====================================================
// Action classes following ESPHome pattern
// =====================================================
template<typename... Ts> 
class SdImageLoadAction : public Action<Ts...> {
 public:
  explicit SdImageLoadAction(SdImageComponent *parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::string, file_path)
  
  void play(Ts... x) override {
    if (this->parent_ == nullptr) return;
    
    if (this->file_path_.has_value()) {
      std::string path = this->file_path_.value(x...);
      if (!path.empty()) {
        this->parent_->load_image_from_path(path);
        return;
      }
    }
    
    this->parent_->load_image();
  }

 private:
  SdImageComponent *parent_;
};

template<typename... Ts> 
class SdImageUnloadAction : public Action<Ts...> {
 public:
  explicit SdImageUnloadAction(SdImageComponent *parent) : parent_(parent) {}
  
  void play(Ts... x) override {
    if (this->parent_ != nullptr) {
      this->parent_->unload_image();
    }
  }

 private:
  SdImageComponent *parent_;
};

// NOUVEAU: Actions pour contrôle global
template<typename... Ts> 
class StorageLoadAllAction : public Action<Ts...> {
 public:
  explicit StorageLoadAllAction(StorageComponent *parent) : parent_(parent) {}
  
  void play(Ts... x) override {
    if (this->parent_ != nullptr) {
      this->parent_->load_all_images();
    }
  }

 private:
  StorageComponent *parent_;
};

template<typename... Ts> 
class StorageUnloadAllAction : public Action<Ts...> {
 public:
  explicit StorageUnloadAllAction(StorageComponent *parent) : parent_(parent) {}
  
  void play(Ts... x) override {
    if (this->parent_ != nullptr) {
      this->parent_->unload_all_images();
    }
  }

 private:
  StorageComponent *parent_;
};

}  // namespace storage
}  // namespace esphome







