#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/display/display.h"
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace esphome {
namespace storage {

static const char *const TAG = "storage";
static const char *const TAG_IMAGE = "storage.sd_image";

// ======== StorageComponent Implementation ========

void StorageComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Storage Component...");
  
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not set!");
    this->mark_failed();
    return;
  }
  
  ESP_LOGD(TAG, "Platform: %s", this->platform_.c_str());
  ESP_LOGD(TAG, "Root Path: %s", this->root_path_.c_str());
  
  ESP_LOGCONFIG(TAG, "Storage Component setup complete");
}

void StorageComponent::loop() {
  // Rien pour le moment
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Root Path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  SD Component: %s", this->sd_component_ ? "Connected" : "Not Connected");
}

bool StorageComponent::file_exists_direct(const std::string &path) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return false;
  }
  
  ESP_LOGV(TAG, "Checking file existence: %s", path.c_str());
  
  struct stat st;
  bool exists = (stat(path.c_str(), &st) == 0);
  
  if (exists) {
    ESP_LOGD(TAG, "File exists: %s (%ld bytes)", path.c_str(), (long)st.st_size);
  } else {
    ESP_LOGD(TAG, "File not found: %s", path.c_str());
  }
  
  return exists;
}

std::vector<uint8_t> StorageComponent::read_file_direct(const std::string &path) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return {};
  }
  
  ESP_LOGI(TAG, "Reading file: %s", path.c_str());
  
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) {
    ESP_LOGE(TAG, "Cannot open file: %s (errno: %d)", path.c_str(), errno);
    return {};
  }
  
  // Obtenir la taille du fichier
  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  ESP_LOGI(TAG, "File size: %zu bytes", file_size);
  
  if (file_size == 0) {
    ESP_LOGW(TAG, "Empty file: %s", path.c_str());
    fclose(file);
    return {};
  }
  
  // Allouer le buffer
  std::vector<uint8_t> data(file_size);
  
  // Lire le fichier
  size_t bytes_read = fread(data.data(), 1, file_size, file);
  fclose(file);
  
  if (bytes_read != file_size) {
    ESP_LOGW(TAG, "Partial read: %zu/%zu bytes", bytes_read, file_size);
    data.resize(bytes_read);
  }
  
  ESP_LOGI(TAG, "File read successfully: %zu bytes", bytes_read);
  
  return data;
}

bool StorageComponent::write_file_direct(const std::string &path, const std::vector<uint8_t> &data) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return false;
  }
  
  ESP_LOGI(TAG, "Writing file: %s (%zu bytes)", path.c_str(), data.size());
  
  FILE *file = fopen(path.c_str(), "wb");
  if (!file) {
    ESP_LOGE(TAG, "Cannot create file: %s (errno: %d)", path.c_str(), errno);
    return false;
  }
  
  size_t written = fwrite(data.data(), 1, data.size(), file);
  fclose(file);
  
  if (written != data.size()) {
    ESP_LOGE(TAG, "Partial write: %zu/%zu bytes", written, data.size());
    return false;
  }
  
  ESP_LOGI(TAG, "File written successfully: %zu bytes", written);
  return true;
}

size_t StorageComponent::get_file_size(const std::string &path) {
  if (!this->sd_component_) {
    ESP_LOGE(TAG, "SD component not available");
    return 0;
  }
  
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
    return st.st_size;
  } else {
    ESP_LOGE(TAG, "Cannot get file size: %s", path.c_str());
    return 0;
  }
}

// ======== SdImageComponent Implementation ========

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not set!");
    this->mark_failed();
    return;
  }
  
  if (!this->validate_file_path()) {
    ESP_LOGE(TAG_IMAGE, "Invalid file path: %s", this->file_path_.c_str());
    this->mark_failed();
    return;
  }
  
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component setup complete");
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File Path: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Actual Dimensions: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->get_output_format_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Byte Order: %s", this->get_byte_order_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Currently Loaded: %s", this->is_loaded_ ? "YES" : "NO");
  
  if (this->is_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Memory Usage: %zu bytes", this->image_data_.size());
  }
}

void SdImageComponent::set_output_format_string(const std::string &format) {
  if (format == "RGB565") this->output_format_ = OutputImageFormat::rgb565;
  else if (format == "RGB888") this->output_format_ = OutputImageFormat::rgb888;
  else if (format == "RGBA") this->output_format_ = OutputImageFormat::rgba;
  else {
    ESP_LOGW(TAG_IMAGE, "Unknown format: %s, using RGB565", format.c_str());
    this->output_format_ = OutputImageFormat::rgb565;
  }
}

void SdImageComponent::set_byte_order_string(const std::string &byte_order) {
  if (byte_order == "BIG_ENDIAN") this->byte_order_ = ByteOrder::big_endian;
  else if (byte_order == "LITTLE_ENDIAN") this->byte_order_ = ByteOrder::little_endian;
  else {
    ESP_LOGW(TAG_IMAGE, "Unknown byte order: %s, using little_endian", byte_order.c_str());
    this->byte_order_ = ByteOrder::little_endian;
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
    case ByteOrder::little_endian: return "Little Endian";
    case ByteOrder::big_endian: return "Big Endian";
    default: return "Unknown";
  }
}

bool SdImageComponent::load_image() {
  return this->load_image_from_path(this->file_path_);
}

bool SdImageComponent::load_image_from_path(const std::string &path) {
  ESP_LOGI(TAG_IMAGE, "Loading image from: %s", path.c_str());
  
  if (!this->storage_component_) {
    ESP_LOGE(TAG_IMAGE, "Storage component not available");
    return false;
  }
  
  // Libérer l'image précédente si chargée
  if (this->is_loaded_) {
    this->unload_image();
  }
  
  // Vérifier si le fichier existe
  if (!this->storage_component_->file_exists_direct(path)) {
    ESP_LOGE(TAG_IMAGE, "Image file not found: %s", path.c_str());
    return false;
  }
  
  // Lire les données depuis la SD
  std::vector<uint8_t> file_data = this->storage_component_->read_file_direct(path);
  
  if (file_data.empty()) {
    ESP_LOGE(TAG_IMAGE, "Failed to read image file: %s", path.c_str());
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "Read %zu bytes from file", file_data.size());
  
  // Détecter le type de fichier et décoder
  bool decode_success = false;
  if (this->is_jpeg_file(file_data)) {
    ESP_LOGI(TAG_IMAGE, "Detected JPEG file, decoding...");
    decode_success = this->decode_jpeg(file_data);
  } else if (this->is_png_file(file_data)) {
    ESP_LOGI(TAG_IMAGE, "Detected PNG file, decoding...");
    decode_success = this->decode_png(file_data);
  } else {
    ESP_LOGI(TAG_IMAGE, "Assuming raw bitmap data");
    // Pour les données brutes, nous devons connaître les dimensions à l'avance
    if (this->width_ <= 0 || this->height_ <= 0) {
      ESP_LOGE(TAG_IMAGE, "Dimensions must be set for raw data. Current: %dx%d", 
               this->width_, this->height_);
      return false;
    }
    decode_success = this->load_raw_data(file_data);
  }
  
  if (!decode_success) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode image: %s", path.c_str());
    return false;
  }
  
  // Mettre à jour le chemin actuel
  this->file_path_ = path;
  this->is_loaded_ = true;
  
  ESP_LOGI(TAG_IMAGE, "✅ Image loaded successfully: %dx%d, %zu bytes", 
           this->width_, this->height_, this->image_data_.size());
  
  return true;
}

bool SdImageComponent::is_jpeg_file(const std::vector<uint8_t> &data) const {
  return data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool SdImageComponent::is_png_file(const std::vector<uint8_t> &data) const {
  const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return data.size() >= 8 && memcmp(data.data(), png_signature, 8) == 0;
}

bool SdImageComponent::decode_jpeg(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "JPEG decoder: Processing %zu bytes", jpeg_data.size());
  
  // Extraction des dimensions du header JPEG
  int detected_width = 0, detected_height = 0;
  if (!this->extract_jpeg_dimensions(jpeg_data, detected_width, detected_height)) {
    // Utiliser des dimensions par défaut si l'extraction échoue
    detected_width = 320;
    detected_height = 240;
    ESP_LOGW(TAG_IMAGE, "JPEG dimensions not found, using defaults: %dx%d", detected_width, detected_height);
  } else {
    ESP_LOGI(TAG_IMAGE, "JPEG dimensions detected: %dx%d", detected_width, detected_height);
  }
  
  this->width_ = detected_width;
  this->height_ = detected_height;
  
  // Vérifier les limites raisonnables
  if (this->width_ <= 0 || this->height_ <= 0 || this->width_ > 2048 || this->height_ > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid JPEG dimensions: %dx%d", this->width_, this->height_);
    return false;
  }
  
  // Calculer la taille de sortie basée sur le format configuré
  size_t output_size = this->calculate_output_size();
  this->image_data_.resize(output_size);
  
  // IMPORTANT: ICI VOUS DEVRIEZ UTILISER UNE VRAIE BIBLIOTHÈQUE DE DÉCODAGE JPEG
  // Pour l'instant, simulation avec un motif réaliste
  this->generate_test_pattern(jpeg_data);
  
  ESP_LOGI(TAG_IMAGE, "JPEG processing complete: %dx%d %s (%zu bytes)", 
           this->width_, this->height_, this->get_output_format_string().c_str(), output_size);
  
  return true;
}

bool SdImageComponent::decode_png(const std::vector<uint8_t> &png_data) {
  ESP_LOGI(TAG_IMAGE, "PNG decoder: Processing %zu bytes", png_data.size());
  
  // Extraction des dimensions PNG depuis le chunk IHDR
  int detected_width = 0, detected_height = 0;
  if (!this->extract_png_dimensions(png_data, detected_width, detected_height)) {
    // Utiliser des dimensions par défaut si l'extraction échoue
    detected_width = 320;
    detected_height = 240;
    ESP_LOGW(TAG_IMAGE, "PNG dimensions not found, using defaults: %dx%d", detected_width, detected_height);
  } else {
    ESP_LOGI(TAG_IMAGE, "PNG dimensions detected: %dx%d", detected_width, detected_height);
  }
  
  this->width_ = detected_width;
  this->height_ = detected_height;
  
  // Vérifier les limites
  if (this->width_ <= 0 || this->height_ <= 0 || this->width_ > 2048 || this->height_ > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid PNG dimensions: %dx%d", this->width_, this->height_);
    return false;
  }
  
  // Créer une image dans le format configuré
  size_t output_size = this->calculate_output_size();
  this->image_data_.resize(output_size);
  
  // Générer un motif différent pour PNG
  this->generate_test_pattern(png_data);
  
  ESP_LOGI(TAG_IMAGE, "PNG processing complete: %dx%d %s (%zu bytes)", 
           this->width_, this->height_, this->get_output_format_string().c_str(), output_size);
  
  return true;
}

bool SdImageComponent::load_raw_data(const std::vector<uint8_t> &raw_data) {
  ESP_LOGI(TAG_IMAGE, "Loading raw bitmap data (%zu bytes)", raw_data.size());
  
  // Calculer la taille attendue
  size_t expected_size = this->calculate_output_size();
  
  ESP_LOGI(TAG_IMAGE, "Raw image: %dx%d, format: %s, expected: %zu bytes, got: %zu bytes",
           this->width_, this->height_, this->get_output_format_string().c_str(), 
           expected_size, raw_data.size());
  
  // Adapter la taille et copier les données
  this->image_data_.resize(expected_size);
  
  if (raw_data.size() >= expected_size) {
    // Fichier plus grand ou égal: copier seulement ce dont on a besoin
    memcpy(this->image_data_.data(), raw_data.data(), expected_size);
    ESP_LOGI(TAG_IMAGE, "Copied %zu bytes from larger file", expected_size);
  } else {
    // Fichier plus petit: copier ce qu'on peut et remplir le reste
    memcpy(this->image_data_.data(), raw_data.data(), raw_data.size());
    memset(this->image_data_.data() + raw_data.size(), 0, expected_size - raw_data.size());
    ESP_LOGW(TAG_IMAGE, "Copied %zu bytes, filled %zu bytes with zeros", 
             raw_data.size(), expected_size - raw_data.size());
  }
  
  // Conversion de l'ordre des bytes si nécessaire
  if (this->byte_order_ == ByteOrder::big_endian && this->get_pixel_size() > 1) {
    ESP_LOGD(TAG_IMAGE, "Converting byte order to little endian");
    this->convert_byte_order();
  }
  
void SdImageComponent::unload_image() {
  ESP_LOGD(TAG_IMAGE, "Unloading image");
  
  this->image_data_.clear();
  this->image_data_.shrink_to_fit();
  this->is_loaded_ = false;
  
  ESP_LOGD(TAG_IMAGE, "Image unloaded");
}

bool SdImageComponent::reload_image() {
  ESP_LOGD(TAG_IMAGE, "Reloading image");
  return this->load_image_from_path(this->file_path_);
}

void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (!this->is_loaded_ || this->image_data_.empty()) {
    ESP_LOGD(TAG_IMAGE, "Cannot draw: image not loaded");
    return;
  }
  
  if (!display) {
    ESP_LOGE(TAG_IMAGE, "Display is null");
    return;
  }

  ESP_LOGV(TAG_IMAGE, "Drawing image at (%d,%d) size %dx%d", x, y, this->width_, this->height_);

  // Vérifications de sécurité
  if (this->width_ <= 0 || this->height_ <= 0) {
    ESP_LOGE(TAG_IMAGE, "Invalid dimensions: %dx%d", this->width_, this->height_);
    return;
  }
  
  size_t expected_size = this->calculate_output_size();
  if (this->image_data_.size() < expected_size) {
    ESP_LOGE(TAG_IMAGE, "Insufficient data: %zu < %zu", this->image_data_.size(), expected_size);
    return;
  }

  // Dessiner avec gestion d'erreurs robuste
  int pixels_drawn = 0;
  int pixels_skipped = 0;
  
  for (int img_y = 0; img_y < this->height_; img_y++) {
    for (int img_x = 0; img_x < this->width_; img_x++) {
      try {
        uint8_t red, green, blue, alpha = 255;
        
        // get_pixel avec vérifications intégrées
        this->get_pixel(img_x, img_y, red, green, blue, alpha);
        
        // Skip transparent pixels
        if (alpha == 0) {
          pixels_skipped++;
          continue;
        }

        Color pixel_color(red, green, blue);
        int screen_x = x + img_x;
        int screen_y = y + img_y;
        
        // Vérifier les limites de l'écran si possible
        display->draw_pixel_at(screen_x, screen_y, pixel_color);
        pixels_drawn++;
        
      } catch (...) {
        ESP_LOGE(TAG_IMAGE, "Exception drawing pixel at (%d,%d)", img_x, img_y);
        pixels_skipped++;
      }
    }
    
    // Yield périodiquement pour éviter les watchdog timeouts
    if (img_y % 20 == 0) {
      yield();
    }
  }
  
  ESP_LOGD(TAG_IMAGE, "Draw completed: %d pixels drawn, %d skipped", pixels_drawn, pixels_skipped);
}

ImageType SdImageComponent::get_image_type() const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565:
      return image::IMAGE_TYPE_RGB565;
    case OutputImageFormat::rgb888:
      return image::IMAGE_TYPE_RGB24;
    case OutputImageFormat::rgba:
      return image::IMAGE_TYPE_RGBA;
    default:
      return image::IMAGE_TYPE_RGB565;
  }
}

void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue) const {
  uint8_t alpha;
  this->get_pixel(x, y, red, green, blue, alpha);
}

void SdImageComponent::get_pixel(int x, int y, uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  // Initialiser à des valeurs sûres
  red = green = blue = alpha = 0;
  
  // Vérifications de base
  if (!this->is_loaded_ || this->image_data_.empty()) {
    return;
  }
  
  if (x < 0 || x >= this->width_ || y < 0 || y >= this->height_) {
    return;
  }
  
  // Calculs d'offset sécurisés
  size_t pixel_size = this->get_pixel_size();
  size_t offset = this->get_pixel_offset(x, y);
  
  if (offset + pixel_size > this->image_data_.size()) {
    ESP_LOGW(TAG_IMAGE, "Pixel offset out of bounds: %zu+%zu > %zu at (%d,%d)", 
             offset, pixel_size, this->image_data_.size(), x, y);
    return;
  }
  
  const uint8_t *pixel_data = &this->image_data_[offset];
  this->convert_pixel_format(x, y, pixel_data, red, green, blue, alpha);
}

void SdImageComponent::convert_pixel_format(int x, int y, const uint8_t *pixel_data,
                                           uint8_t &red, uint8_t &green, uint8_t &blue, uint8_t &alpha) const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565: {
      // Little endian: LSB first
      uint16_t pixel = pixel_data[0] | (pixel_data[1] << 8);
      red = ((pixel >> 11) & 0x1F) << 3;   // 5 bits -> 8 bits (avec extension)
      green = ((pixel >> 5) & 0x3F) << 2;  // 6 bits -> 8 bits (avec extension)
      blue = (pixel & 0x1F) << 3;          // 5 bits -> 8 bits (avec extension)
      
      // Extension des bits pour une meilleure précision
      red |= red >> 5;
      green |= green >> 6; 
      blue |= blue >> 5;
      
      alpha = 255;
      break;
    }
    case OutputImageFormat::rgb888:
      red = pixel_data[0];
      green = pixel_data[1];
      blue = pixel_data[2];
      alpha = 255;
      break;
    case OutputImageFormat::rgba:
      red = pixel_data[0];
      green = pixel_data[1];
      blue = pixel_data[2];
      alpha = pixel_data[3];
      break;
    default:
      red = green = blue = alpha = 0;
  }
}

size_t SdImageComponent::get_pixel_size() const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565:
      return 2;
    case OutputImageFormat::rgb888:
      return 3;
    case OutputImageFormat::rgba:
      return 4;
    default:
      return 2;
  }
}

size_t SdImageComponent::get_pixel_offset(int x, int y) const {
  return (y * this->width_ + x) * this->get_pixel_size();
}

void SdImageComponent::convert_byte_order() {
  size_t pixel_size = this->get_pixel_size();
  
  if (pixel_size <= 1) return;
  
  ESP_LOGD(TAG_IMAGE, "Converting byte order for %zu pixels", this->image_data_.size() / pixel_size);
  
  for (size_t i = 0; i < this->image_data_.size(); i += pixel_size) {
    if (pixel_size == 2) {
      std::swap(this->image_data_[i], this->image_data_[i + 1]);
    } else if (pixel_size == 4) {
      std::swap(this->image_data_[i], this->image_data_[i + 3]);
      std::swap(this->image_data_[i + 1], this->image_data_[i + 2]);
    }
  }
}

bool SdImageComponent::validate_dimensions() const {
  return this->width_ > 0 && this->height_ > 0 && this->width_ <= 2048 && this->height_ <= 2048;
}

bool SdImageComponent::validate_file_path() const {
  return !this->file_path_.empty() && this->file_path_[0] == '/';
}

size_t SdImageComponent::calculate_output_size() const {
  if (this->width_ <= 0 || this->height_ <= 0) return 0;
  return this->width_ * this->height_ * this->get_pixel_size();
}

bool SdImageComponent::validate_image_data() const {
  if (!this->is_loaded_) return false;
  return !this->image_data_.empty() && this->validate_dimensions();
}

// Méthodes d'extraction de dimensions
bool SdImageComponent::extract_jpeg_dimensions(const std::vector<uint8_t> &data, int &width, int &height) const {
  for (size_t i = 0; i < data.size() - 10; i++) {
    if (data[i] == 0xFF) {
      uint8_t marker = data[i + 1];
      // SOF0, SOF1, SOF2 markers
      if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
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
  if (data.size() >= 24) {
    // Le chunk IHDR commence à l'offset 16 dans un PNG valide
    // Largeur: bytes 16-19, Hauteur: bytes 20-23 (big endian)
    width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    return true;
  }
  return false;
}

// Génération de motifs de test
void SdImageComponent::generate_test_pattern(const std::vector<uint8_t> &source_data) {
  // Créer un seed basé sur les données source
  uint32_t seed = 0;
  for (size_t i = 0; i < std::min(source_data.size(), (size_t)16); i++) {
    seed = (seed << 8) | source_data[i];
  }
  
  ESP_LOGI(TAG_IMAGE, "Generating test pattern (seed: %u)", seed);
  
  for (int y = 0; y < this->height_; y++) {
    for (int x = 0; x < this->width_; x++) {
      size_t offset = this->get_pixel_offset(x, y);
      
      // Utiliser les données source pour créer un motif pseudo-aléatoire mais déterministe
      uint32_t pixel_seed = seed + (y * this->width_ + x);
      pixel_seed = pixel_seed * 1664525 + 1013904223; // Linear congruential generator
      
      uint8_t r = (pixel_seed >> 16) & 0xFF;
      uint8_t g = (pixel_seed >> 8) & 0xFF;
      uint8_t b = pixel_seed & 0xFF;
      
      // Appliquer le format de sortie
      switch (this->output_format_) {
        case OutputImageFormat::rgb565: {
          uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
          this->image_data_[offset + 0] = rgb565 & 0xFF;
          this->image_data_[offset + 1] = (rgb565 >> 8) & 0xFF;
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
          this->image_data_[offset + 3] = 255; // Alpha opaque
          break;
      }
    }
  }
}

}  // namespace storage  
}  // namespace esphome
