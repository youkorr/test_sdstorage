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

namespace esphome {
namespace storage {

// Forward declarations
class StorageComponent;

// Utiliser l'enum ImageType de ESPHome
using ImageType = image::ImageType;

// Énumérations pour les formats d'image (JPEG/PNG uniquement)
enum class OutputImageFormat {
  rgb565,
  rgb888,
  rgba
};

// Classe principale Storage (simplifiée)
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
  
  // Méthodes de fichier
  bool file_exists_direct(const std::string &path);
  std::vector<uint8_t> read_file_direct(const std::string &path);
  bool write_file_direct(const std::string &path, const std::vector<uint8_t> &data);
  size_t get_file_size(const std::string &path);
  
  // Getters
  const std::string &get_platform() const { return this->platform_; }
  sd_mmc_card::SdMmc *get_sd_component() const { return this->sd_component_; }
  
 private:
  std::string platform_;
  sd_mmc_card::SdMmc *sd_component_{nullptr};
};

// Classe pour les images SD - HÉRITE CORRECTEMENT de display::BaseImage
class SdImageComponent : public Component, public display::BaseImage {
 public:
  SdImageComponent() = default;

  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration de base
  void set_file_path(const std::string &path) { this->file_path_ = path; }
  void set_output_format(OutputImageFormat format) { this->output_format_ = format; }
  void set_output_format_string(const std::string &format);
  void set_storage_component(StorageComponent *storage) { this->storage_component_ = storage; }
  
  // Getters
  const std::string &get_file_path() const { return this->file_path_; }
  int get_width() const override { return this->width_; }
  int get_height() const override { return this->height_; }
  OutputImageFormat get_output_format() const { return this->output_format_; }
  bool is_loaded() const { return this->is_loaded_; }
  
  // Méthodes héritées de display::BaseImage
  void draw(int x, int y, display::Display *display, Color color_on, Color color_off) override;
  
  // Accès aux données image
  const uint8_t *get_data_start() const { 
    return this->image_data_.empty() ? nullptr : this->image_data_.data(); 
  }
  ImageType get_image_type() const;
  
  // Chargement/déchargement d'image (simplifié)
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // Accès aux pixels avec vérifications de sécurité
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const;
  void get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const; 
  const uint8_t *get_data() const { 
    return this->image_data_.empty() ? nullptr : this->image_data_.data(); 
  }
  size_t get_data_size() const { return this->image_data_.size(); }
  
  // Méthodes utilitaires
  bool validate_image_data() const;
  std::string get_output_format_string() const;
  
  // Compatibilité affichage
  void get_image_dimensions(int *width, int *height) const {
    if (width) *width = this->width_;
    if (height) *height = this->height_;
  }
  
  const uint8_t* get_image_data() const { return this->get_data(); }

  // Méthodes utilitaires pour le diagnostic
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

 private:
  // Configuration
  std::string file_path_;
  int width_{0};
  int height_{0};
  OutputImageFormat output_format_{OutputImageFormat::rgb565};
  
  // État
  bool is_loaded_{false};
  std::vector<uint8_t> image_data_;
  StorageComponent *storage_component_{nullptr};
  
  // Méthodes de décodage d'images (JPEG/PNG uniquement)
  bool is_jpeg_file(const std::vector<uint8_t> &data) const;
  bool is_png_file(const std::vector<uint8_t> &data) const;
  bool decode_jpeg(const std::vector<uint8_t> &jpeg_data);
  bool decode_png(const std::vector<uint8_t> &png_data);
  
  // Méthodes privées pour l'extraction de métadonnées
  bool extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  bool extract_png_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const;
  
  // Méthodes de conversion et validation
  void convert_pixel_format(int x, int y, const uint8_t *pixel_data, 
                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const;
  size_t get_pixel_size() const;
  size_t get_pixel_offset(int x, int y) const;
  
  bool validate_dimensions() const;
  bool validate_file_path() const;
  bool validate_pixel_access(int x, int y) const;
  
  // Méthodes utilitaires
  std::string detect_file_type(const std::string &path) const;
  bool is_supported_format(const std::string &extension) const;
};

// Actions pour l'automatisation avec gestion d'erreurs améliorée
template<typename... Ts> 
class SdImageLoadAction : public Action<Ts...> {
 public:
  SdImageLoadAction() = default;
  explicit SdImageLoadAction(SdImageComponent *parent) : parent_(parent) {}
  
  TEMPLATABLE_VALUE(std::string, file_path)
  
  void set_parent(SdImageComponent *parent) { this->parent_ = parent; }
  
  void play(Ts... x) override {
    if (this->parent_ == nullptr) {
      ESP_LOGE("sd_image.load", "Parent component is null");
      return;
    }
    
    try {
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
      
      ESP_LOGD("sd_image.load", "Loading image from configured path");
      if (!this->parent_->load_image()) {
        ESP_LOGE("sd_image.load", "Failed to load image from configured path");
      }
    } catch (const std::exception& e) {
      ESP_LOGE("sd_image.load", "Exception during image loading: %s", e.what());
    } catch (...) {
      ESP_LOGE("sd_image.load", "Unknown exception during image loading");
    }
  }

 private:
  SdImageComponent *parent_{nullptr};
};

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
    
    try {
      ESP_LOGD("sd_image.unload", "Unloading image: %s", this->parent_->get_debug_info().c_str());
      this->parent_->unload_image();
      ESP_LOGD("sd_image.unload", "Image unloaded successfully");
    } catch (const std::exception& e) {
      ESP_LOGE("sd_image.unload", "Exception during image unloading: %s", e.what());
    } catch (...) {
      ESP_LOGE("sd_image.unload", "Unknown exception during image unloading");
    }
  }

 private:
  SdImageComponent *parent_{nullptr};
};

}  // namespace storage
}  // namespace esphome
