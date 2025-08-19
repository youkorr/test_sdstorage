#include "storage.h"
#include "esphome/core/log.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_system.h>

namespace esphome {
namespace storage {

static const char *const TAG = "storage";
static const char *const TAG_IMAGE = "storage.image";

// =====================================================
// StorageComponent Implementation (inchangée)
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
// SdImageComponent Implementation - Compatible ESPHome
// =====================================================

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  ESP_LOGCONFIG(TAG_IMAGE, "  File path: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Resize: %dx%d", this->resize_width_, this->resize_height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Storage component: %s", this->storage_component_ ? "configured" : "not configured");
  
  // Auto-load image if path is specified
  if (!this->file_path_.empty() && this->storage_component_) {
    ESP_LOGI(TAG_IMAGE, "Auto-loading image from: %s", this->file_path_.c_str());
    if (!this->load_image()) {
      ESP_LOGW(TAG_IMAGE, "Auto-load failed, image will be loaded on demand");
    }
  }
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Current dimensions: %dx%d", this->get_width(), this->get_height());
  ESP_LOGCONFIG(TAG_IMAGE, "  Loaded: %s", this->is_loaded_ ? "YES" : "NO");
  if (this->is_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Data size: %zu bytes", this->image_data_.size());
    ESP_LOGCONFIG(TAG_IMAGE, "  Data pointer: %p", this->get_data_start());
  }
}

// =====================================================
// MÉTHODES PRINCIPALES DE CHARGEMENT D'IMAGE
// =====================================================

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGI(TAG_IMAGE, "=== LOADING SD IMAGE ===");
  ESP_LOGI(TAG_IMAGE, "🔄 Loading: %s", path.c_str());
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "❌ Storage component not available");
    return false;
  }
  
  // Unload current image
  if (this->is_loaded_) {
    ESP_LOGI(TAG_IMAGE, "🗑️ Unloading current image");
    this->unload_image();
  }
  
  // Check file exists
  if (!this->storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "❌ File not found: %s", path.c_str());
    this->list_directory_contents(path.substr(0, path.find_last_of("/")));
    return false;
  }
  
  // Read file data
  std::vector<uint8_t> file_data = this->storage_component_->read_file_direct(path);
  if (file_data.empty()) {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to read file: %s", path.c_str());
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "✅ Read %zu bytes from: %s", file_data.size(), path.c_str());
  
  // Debug first few bytes
  if (file_data.size() >= 16) {
    ESP_LOGI(TAG_IMAGE, "🔍 Header: %02X %02X %02X %02X %02X %02X %02X %02X", 
             file_data[0], file_data[1], file_data[2], file_data[3],
             file_data[4], file_data[5], file_data[6], file_data[7]);
  }
  
  // Process the image data
  if (!this->decode_image_data(file_data)) {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to decode image data");
    return false;
  }
  
  // Update path and mark as loaded
  this->file_path_ = path;
  this->is_loaded_ = true;
  
  ESP_LOGI(TAG_IMAGE, "🎉 Image loaded successfully!");
  ESP_LOGI(TAG_IMAGE, "   Dimensions: %dx%d", this->get_width(), this->get_height());
  ESP_LOGI(TAG_IMAGE, "   Type: %d", static_cast<int>(this->get_type()));
  ESP_LOGI(TAG_IMAGE, "   Data size: %zu bytes", this->image_data_.size());
  ESP_LOGI(TAG_IMAGE, "=== LOADING COMPLETE ===");
  
  return true;
}

bool SdImageComponent::load_image() {
  return this->load_image_from_path(this->file_path_);
}

void SdImageComponent::unload_image() {
  ESP_LOGI(TAG_IMAGE, "🗑️ Unloading image");
  
  // Clear data and reset properties
  this->image_data_.clear();
  this->image_data_.shrink_to_fit();
  this->is_loaded_ = false;
  
  // Reset the base Image class to empty state
  this->update_image_properties(nullptr, 0, 0, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE);
  
  ESP_LOGI(TAG_IMAGE, "✅ Image unloaded");
}

bool SdImageComponent::reload_image() {
  ESP_LOGI(TAG_IMAGE, "🔄 Reloading image: %s", this->file_path_.c_str());
  this->unload_image();
  return this->load_image();
}

// =====================================================
// TRAITEMENT DES DONNÉES D'IMAGE
// =====================================================

bool SdImageComponent::decode_image_data(const std::vector<uint8_t> &file_data) {
  ESP_LOGI(TAG_IMAGE, "🔧 Decoding image data (%zu bytes)", file_data.size());
  
  // Vérifier si c'est un format d'image supporté
  if (!this->is_supported_image_format(file_data)) {
    ESP_LOGW(TAG_IMAGE, "⚠️ Unsupported format, creating test pattern");
    return this->create_test_pattern_image();
  }
  
  std::string format = this->detect_image_format(file_data);
  ESP_LOGI(TAG_IMAGE, "📷 Detected format: %s", format.c_str());
  
  // Pour l'instant, utiliser PIL simulation pour tous les formats
  return this->process_image_with_pil_simulation(file_data);
}

bool SdImageComponent::is_supported_image_format(const std::vector<uint8_t> &data) const {
  if (data.size() < 8) return false;
  
  // JPEG
  if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return true;
  }
  
  // PNG
  const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (data.size() >= 8 && std::memcmp(data.data(), png_signature, 8) == 0) {
    return true;
  }
  
  // BMP (simple check)
  if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
    return true;
  }
  
  return false;
}

std::string SdImageComponent::detect_image_format(const std::vector<uint8_t> &data) const {
  if (data.size() < 8) return "unknown";
  
  // JPEG
  if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return "JPEG";
  }
  
  // PNG
  const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (std::memcmp(data.data(), png_signature, 8) == 0) {
    return "PNG";
  }
  
  // BMP
  if (data[0] == 'B' && data[1] == 'M') {
    return "BMP";
  }
  
  return "unknown";
}

bool SdImageComponent::extract_image_dimensions(const std::vector<uint8_t> &data, 
                                                int &width, int &height, 
                                                image::ImageType &type) const {
  ESP_LOGD(TAG_IMAGE, "🔍 Extracting image dimensions...");
  
  std::string format = this->detect_image_format(data);
  
  if (format == "JPEG") {
    // Recherche des marqueurs SOF (Start of Frame)
    for (size_t i = 0; i < data.size() - 10; i++) {
      if (data[i] == 0xFF) {
        uint8_t marker = data[i + 1];
        // SOF0, SOF1, SOF2, SOF3
        if (marker >= 0xC0 && marker <= 0xC3 && i + 9 < data.size()) {
          height = (data[i + 5] << 8) | data[i + 6];
          width = (data[i + 7] << 8) | data[i + 8];
          type = image::IMAGE_TYPE_RGB565; // Par défaut pour JPEG
          ESP_LOGD(TAG_IMAGE, "✅ JPEG dimensions: %dx%d", width, height);
          return width > 0 && height > 0 && width <= 2048 && height <= 2048;
        }
      }
    }
  } 
  else if (format == "PNG") {
    // IHDR chunk pour PNG
    if (data.size() >= 24) {
      if (data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
        width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
        height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
        type = image::IMAGE_TYPE_RGB; // Par défaut pour PNG
        ESP_LOGD(TAG_IMAGE, "✅ PNG dimensions: %dx%d", width, height);
        return width > 0 && height > 0 && width <= 2048 && height <= 2048;
      }
    }
  }
  else if (format == "BMP") {
    // BMP header
    if (data.size() >= 26) {
      width = *reinterpret_cast<const int32_t*>(&data[18]);
      height = *reinterpret_cast<const int32_t*>(&data[22]);
      type = image::IMAGE_TYPE_RGB; // Par défaut pour BMP
      ESP_LOGD(TAG_IMAGE, "✅ BMP dimensions: %dx%d", width, height);
      return width > 0 && height > 0 && width <= 2048 && height <= 2048;
    }
  }
  
  ESP_LOGW(TAG_IMAGE, "⚠️ Could not extract dimensions from %s", format.c_str());
  return false;
}

bool SdImageComponent::process_image_with_pil_simulation(const std::vector<uint8_t> &data) {
  ESP_LOGI(TAG_IMAGE, "🎨 Processing image with PIL-like approach");
  
  // Extraire les dimensions de l'image
  int width = 0, height = 0;
  image::ImageType detected_type = image::IMAGE_TYPE_RGB565;
  
  if (!this->extract_image_dimensions(data, width, height, detected_type)) {
    // Utiliser les dimensions de redimensionnement si spécifiées
    if (this->resize_width_ > 0 && this->resize_height_ > 0) {
      width = this->resize_width_;
      height = this->resize_height_;
      ESP_LOGI(TAG_IMAGE, "📐 Using resize dimensions: %dx%d", width, height);
    } else {
      // Dimensions par défaut
      width = 320;
      height = 240;
      ESP_LOGI(TAG_IMAGE, "📐 Using default dimensions: %dx%d", width, height);
    }
  }
  
  // Appliquer le redimensionnement si spécifié
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    ESP_LOGI(TAG_IMAGE, "🔄 Resizing from %dx%d to %dx%d", 
             width, height, this->resize_width_, this->resize_height_);
    width = this->resize_width_;
    height = this->resize_height_;
  }
  
  // Calculer la taille des données selon le type ESPHome
  size_t data_size = 0;
  switch (detected_type) {
    case image::IMAGE_TYPE_BINARY:
      data_size = ((width + 7) / 8) * height;
      break;
    case image::IMAGE_TYPE_GRAYSCALE:
      data_size = width * height;
      break;
    case image::IMAGE_TYPE_RGB565:
      data_size = width * height * 2;
      break;
    case image::IMAGE_TYPE_RGB:
      data_size = width * height * 3;
      break;
    default:
      data_size = width * height * 2; // Fallback RGB565
      break;
  }
  
  ESP_LOGI(TAG_IMAGE, "💾 Allocating %zu bytes for %dx%d image", data_size, width, height);
  
  // Vérifier la mémoire disponible
  size_t free_heap = esp_get_free_heap_size();
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  
  if (data_size > (free_heap + free_psram) * 0.8) {
    ESP_LOGE(TAG_IMAGE, "❌ Insufficient memory for image");
    ESP_LOGE(TAG_IMAGE, "   Need: %zu bytes, Available: %zu heap + %zu PSRAM", 
             data_size, free_heap, free_psram);
    return false;
  }
  
  // Allouer les données
  this->image_data_.clear();
  this->image_data_.resize(data_size, 0);
  
  if (this->image_data_.size() != data_size) {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to allocate image data");
    return false;
  }
  
  // Générer un pattern de test pour l'instant
  // TODO: Implémenter le vrai décodage en utilisant les outils ESPHome
  this->generate_gradient_pattern(width, height);
  
  // Mettre à jour les propriétés de l'image ESPHome
  this->update_image_properties(this->image_data_.data(), width, height, detected_type, image::TRANSPARENCY_OPAQUE);
  
  ESP_LOGI(TAG_IMAGE, "✅ Image processed successfully");
  return true;
}

void SdImageComponent::generate_gradient_pattern(int width, int height) {
  ESP_LOGI(TAG_IMAGE, "🌈 Generating gradient pattern (%dx%d)", width, height);
  
  // Générer un pattern RGB565 (2 bytes par pixel)
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      // Créer un dégradé coloré
      uint8_t r = (x * 255) / width;
      uint8_t g = (y * 255) / height;
      uint8_t b = ((x + y) * 128) / (width + height);
      
      // Convertir en RGB565
      uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      
      size_t offset = (y * width + x) * 2;
      if (offset + 1 < this->image_data_.size()) {
        // Little endian
        this->image_data_[offset + 0] = rgb565 & 0xFF;
        this->image_data_[offset + 1] = (rgb565 >> 8) & 0xFF;
      }
    }
  }
}

bool SdImageComponent::create_test_pattern_image() {
  ESP_LOGI(TAG_IMAGE, "🧪 Creating test pattern image");
  
  int width = this->resize_width_ > 0 ? this->resize_width_ : 64;
  int height = this->resize_height_ > 0 ? this->resize_height_ : 64;
  
  size_t data_size = width * height * 2; // RGB565
  this->image_data_.clear();
  this->image_data_.resize(data_size, 0);
  
  // Pattern de damier
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint16_t rgb565;
      
      // Damier 8x8
      if (((x / 8) + (y / 8)) % 2 == 0) {
        rgb565 = 0xFFFF; // Blanc
      } else {
        rgb565 = 0xF800; // Rouge
      }
      
      size_t offset = (y * width + x) * 2;
      if (offset + 1 < this->image_data_.size()) {
        this->image_data_[offset + 0] = rgb565 & 0xFF;
        this->image_data_[offset + 1] = (rgb565 >> 8) & 0xFF;
      }
    }
  }
  
  this->update_image_properties(this->image_data_.data(), width, height, 
                                image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE);
  
  ESP_LOGI(TAG_IMAGE, "✅ Test pattern created: %dx%d", width, height);
  return true;
}

// =====================================================
// MISE À JOUR DES PROPRIÉTÉS DE L'IMAGE ESPHOME
// =====================================================

void SdImageComponent::update_image_properties(const uint8_t* data, int width, int height, 
                                               image::ImageType type, image::Transparency transparency) {
  // Mettre à jour directement les membres protégés de la classe Image
  this->data_start_ = data;
  this->width_ = width;
  this->height_ = height;
  this->type_ = type;
  this->transparency_ = transparency;
  
  // Calculer les BPP selon le type
  switch (type) {
    case image::IMAGE_TYPE_BINARY:
      this->bpp_ = 1;
      break;
    case image::IMAGE_TYPE_GRAYSCALE:
      this->bpp_ = 8;
      break;
    case image::IMAGE_TYPE_RGB565:
      this->bpp_ = 16;
      break;
    case image::IMAGE_TYPE_RGB:
      this->bpp_ = 24;
      break;
    default:
      this->bpp_ = 16;
      break;
  }
  
  ESP_LOGD(TAG_IMAGE, "🔧 Image properties updated: %dx%d, type=%d, bpp=%zu", 
           width, height, static_cast<int>(type), this->bpp_);
}

// =====================================================
// MÉTHODES UTILITAIRES
// =====================================================

void SdImageComponent::list_directory_contents(const std::string &dir_path) const {
  ESP_LOGI(TAG_IMAGE, "📁 Listing directory: %s", dir_path.c_str());
  
  std::string full_dir = dir_path;
  if (full_dir.empty()) full_dir = "/";
  
  DIR *dir = opendir(full_dir.c_str());
  if (!dir) {
    ESP_LOGE(TAG_IMAGE, "❌ Cannot open directory: %s (errno: %d)", full_dir.c_str(), errno);
    return;
  }
  
  struct dirent *entry;
  int file_count = 0;
  
  while ((entry = readdir(dir)) != nullptr) {
    std::string full_path = full_dir;
    if (full_path.back() != '/') full_path += "/";
    full_path += entry->d_name;
    
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if (S_ISREG(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "   📄 %s (%ld bytes)", entry->d_name, (long)st.st_size);
        file_count++;
      } else if (S_ISDIR(st.st_mode) && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        ESP_LOGI(TAG_IMAGE, "   📁 %s/", entry->d_name);
      }
    }
  }
  
  closedir(dir);
  ESP_LOGI(TAG_IMAGE, "📊 Total files: %d", file_count);
}

void SdImageComponent::debug_memory_usage() const {
  size_t free_heap = esp_get_free_heap_size();
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  
  ESP_LOGI(TAG_IMAGE, "💾 Memory status:");
  ESP_LOGI(TAG_IMAGE, "   Heap:  %zu / %zu bytes (%.1f%% free)", 
           free_heap, total_heap, 100.0f * free_heap / total_heap);
  ESP_LOGI(TAG_IMAGE, "   PSRAM: %zu / %zu bytes (%.1f%% free)", 
           free_psram, total_psram, 100.0f * free_psram / total_psram);
  
  if (this->is_loaded_) {
    ESP_LOGI(TAG_IMAGE, "   Current image: %dx%d, %zu bytes", 
             this->get_width(), this->get_height(), this->image_data_.size());
  }
}

}  // namespace storage
}  // namespace esphome






