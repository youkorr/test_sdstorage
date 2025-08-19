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

// =====================================================
// StorageComponent - Classe principale Storage (inchangée)
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
  
  // Méthodes de fichier
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
// SdImageComponent - Compatible avec esphome::image::Image
// =====================================================
class SdImageComponent : public Component, public image::Image {
 public:
  // Constructeur qui initialise la classe Image de base
  SdImageComponent() : image::Image(nullptr, 0, 0, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE) {}

  // ===== MÉTHODES COMPONENT =====
  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // ===== CONFIGURATION =====
  void set_file_path(const std::string &path) { this->file_path_ = path; }
  void set_storage_component(StorageComponent *storage) { this->storage_component_ = storage; }
  void set_resize(int width, int height) { 
    this->resize_width_ = width; 
    this->resize_height_ = height; 
  }
  
  // ===== MÉTHODES D'IMAGE DYNAMIQUE =====
  bool load_image();
  bool load_image_from_path(const std::string &path);
  void unload_image();
  bool reload_image();
  
  // ===== GETTERS SPÉCIFIQUES =====
  const std::string &get_file_path() const { return this->file_path_; }
  bool is_loaded() const { return this->is_loaded_; }
  
  // ===== DIAGNOSTIC =====
  std::string get_debug_info() const {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), 
      "SdImage[%s]: %dx%d, loaded=%s, data_ptr=%p",
      this->file_path_.c_str(),
      this->get_width(), this->get_height(),
      this->is_loaded_ ? "yes" : "no",
      this->get_data_start()
    );
    return std::string(buffer);
  }

 protected:
  // ===== DONNÉES DE CONFIGURATION =====
  std::string file_path_;
  int resize_width_{0};
  int resize_height_{0};
  
  // ===== ÉTAT =====
  bool is_loaded_{false};
  std::vector<uint8_t> image_data_; // Stockage des données d'image
  StorageComponent *storage_component_{nullptr};
  
 private:
  // ===== MÉTHODES INTERNES =====
  bool decode_image_data(const std::vector<uint8_t> &file_data);
  bool create_esphome_image_from_data(const std::vector<uint8_t> &processed_data, 
                                      int width, int height, 
                                      image::ImageType type);
  void update_image_properties(const uint8_t* data, int width, int height, 
                               image::ImageType type, image::Transparency transparency);
  
  // ===== DÉTECTION ET TRAITEMENT =====
  bool is_supported_image_format(const std::vector<uint8_t> &data) const;
  std::string detect_image_format(const std::vector<uint8_t> &data) const;
  bool process_image_with_pil_simulation(const std::vector<uint8_t> &data);
  
  // ===== EXTRACTION DE MÉTADONNÉES =====
  bool extract_image_dimensions(const std::vector<uint8_t> &data, 
                                int &width, int &height, 
                                image::ImageType &type) const;
  
  // ===== MÉTHODES DE FALLBACK =====
  bool create_test_pattern_image();
  void generate_gradient_pattern(int width, int height);
  
  // ===== UTILITAIRES =====
  void list_directory_contents(const std::string &dir_path) const;
  void debug_memory_usage() const;
};

// =====================================================
// ACTIONS - Inchangées
// =====================================================
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
    
    ESP_LOGD("sd_image.unload", "Unloading image: %s", this->parent_->get_debug_info().c_str());
    this->parent_->unload_image();
    ESP_LOGD("sd_image.unload", "Image unloaded successfully");
  }

 private:
  SdImageComponent *parent_{nullptr};
};

}  // namespace storage
}  // namespace esphome





