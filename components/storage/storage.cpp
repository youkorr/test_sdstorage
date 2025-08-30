#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <algorithm>
#include <AnimatedGIF.h>


// Include yield function for ESP32/ESP8266
#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define yield() taskYIELD()
#elif defined(ESP8266)
#include <Esp.h>
// yield() is already available on ESP8266
#else
// Fallback for other platforms
#define yield() delayMicroseconds(1)
#endif

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
  ESP_LOGCONFIG(TAG, "  Auto load: %s", this->auto_load_ ? "YES" : "NO (on-demand)");
  ESP_LOGCONFIG(TAG, "  Registered images: %zu", this->sd_images_.size());
  
  if (this->auto_load_) {
    ESP_LOGI(TAG, "Auto-load enabled globally - will load all images during setup");
  } else {
    ESP_LOGI(TAG, "Auto-load disabled - images will load on-demand");
  }
}

void StorageComponent::loop() {
  // Auto-load global avec retry si nécessaire
  if (this->auto_load_) {
    static uint32_t last_auto_load_attempt = 0;
    static bool auto_load_attempted = false;
    
    if (!auto_load_attempted) {
      uint32_t now = millis();
      if (now > 2000) { // Attendre 2s après le boot pour la SD
        ESP_LOGI(TAG, "Attempting global auto-load of all images...");
        this->load_all_images();
        auto_load_attempted = true;
        last_auto_load_attempt = now;
      }
    } else {
      // Retry logic pour les images qui ont échoué
      uint32_t now = millis();
      if (now - last_auto_load_attempt > 10000) { // Retry toutes les 10s
        bool has_failed_images = false;
        for (SdImageComponent* img : this->sd_images_) {
          if (!img->is_loaded()) {
            has_failed_images = true;
            break;
          }
        }
        
        if (has_failed_images) {
          ESP_LOGI(TAG, "Retrying failed image loads...");
          this->load_all_images();
          last_auto_load_attempt = now;
        }
      }
    }
  }
}

void StorageComponent::load_all_images() {
  ESP_LOGI(TAG, "Loading all registered SD images (%zu total)", this->sd_images_.size());
  
  int loaded_count = 0;
  int failed_count = 0;
  
  for (SdImageComponent* img : this->sd_images_) {
    if (img->is_loaded()) {
      loaded_count++;
      continue; // Déjà chargée
    }
    
    ESP_LOGI(TAG, "Auto-loading: %s", img->get_file_path().c_str());
    if (img->load_image()) {
      loaded_count++;
      ESP_LOGI(TAG, "  ✓ Success: %s", img->get_file_path().c_str());
    } else {
      failed_count++;
      ESP_LOGW(TAG, "  ✗ Failed: %s", img->get_file_path().c_str());
    }
    
    // Yield entre les chargements pour éviter les watchdog
    App.feed_wdt();
    yield();
  }
  
  ESP_LOGI(TAG, "Auto-load complete: %d loaded, %d failed, %d total", 
           loaded_count, failed_count, this->sd_images_.size());
}

void StorageComponent::unload_all_images() {
  ESP_LOGI(TAG, "Unloading all registered SD images");
  
  for (SdImageComponent* img : this->sd_images_) {
    img->unload_image();
  }
  
  ESP_LOGI(TAG, "All images unloaded");
}

void StorageComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Storage Component:");
  ESP_LOGCONFIG(TAG, "  Platform: %s", this->platform_.c_str());
  ESP_LOGCONFIG(TAG, "  Root path: %s", this->root_path_.c_str());
  ESP_LOGCONFIG(TAG, "  SD component: %s", this->sd_component_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Auto load: %s", this->auto_load_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Registered images: %zu", this->sd_images_.size());
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
  ESP_LOGCONFIG(TAG_IMAGE, "  Storage component: %s", this->storage_component_ ? "configured" : "not configured");
  
  if (this->storage_component_) {
    bool global_auto_load = this->storage_component_->get_auto_load();
    ESP_LOGCONFIG(TAG_IMAGE, "  Global auto load: %s", global_auto_load ? "YES" : "NO (on-demand)");
    
    if (global_auto_load) {
      ESP_LOGI(TAG_IMAGE, "Image will be loaded by global auto-load system");
    } else {
      ESP_LOGI(TAG_IMAGE, "Image configured for on-demand loading");
    }
  }
  
  // Initialiser les propriétés de base avec des valeurs par défaut
  this->width_ = this->resize_width_ > 0 ? this->resize_width_ : 1;
  this->height_ = this->resize_height_ > 0 ? this->resize_height_ : 1;
  this->type_ = image::IMAGE_TYPE_RGB565;
  this->bpp_ = 16;
  this->data_start_ = nullptr;
}

void SdImageComponent::loop() {
  // Plus de retry individuel - tout est géré au niveau du StorageComponent
  // ou par le système on-demand
}

// NOUVEAU: Méthodes pour LVGL avec chargement automatique intégré
const uint8_t* SdImageComponent::get_image_data_for_lvgl() {
  // Tentative de chargement automatique avant de retourner les données
  if (!this->ensure_loaded()) {
    ESP_LOGW(TAG_IMAGE, "Failed to auto-load image for LVGL: %s", this->file_path_.c_str());
    return nullptr;
  }
  
  return this->image_buffer_.empty() ? nullptr : this->image_buffer_.data();
}

size_t SdImageComponent::get_image_data_size_for_lvgl() {
  // Tentative de chargement automatique avant de retourner la taille
  if (!this->ensure_loaded()) {
    ESP_LOGW(TAG_IMAGE, "Failed to auto-load image for LVGL: %s", this->file_path_.c_str());
    return 0;
  }
  
  return this->image_buffer_.size();
}

bool SdImageComponent::ensure_loaded() {
  // Si déjà chargée, OK
  if (this->image_loaded_ && !this->image_buffer_.empty()) {
    return true;
  }
  
  // Si auto_load global est activé, attendre que le système global charge
  if (this->should_auto_load()) {
    // En mode auto-load global, on fait un seul essai on-demand si pas encore chargé
    if (this->load_state_ == LoadState::NOT_LOADED) {
      ESP_LOGI(TAG_IMAGE, "Global auto-load active but image not loaded yet, trying once: %s", 
               this->file_path_.c_str());
      return this->load_image();
    }
    return false; // Laisser le système global gérer
  }
  
  // Mode on-demand pur (auto_load global = false)
  if (this->load_state_ == LoadState::LOADING) {
    return false;
  }
  
  if (this->load_state_ == LoadState::FAILED) {
    uint32_t now = millis();
    if (now - this->last_load_attempt_ < LOAD_RETRY_DELAY_MS) {
      return false;
    }
    if (this->load_retry_count_ >= MAX_LOAD_RETRIES) {
      return false;
    }
  }
  
  ESP_LOGI(TAG_IMAGE, "On-demand loading: %s", this->file_path_.c_str());
  
  this->load_state_ = LoadState::LOADING;
  this->last_load_attempt_ = millis();
  
  bool success = this->load_image_from_path(this->file_path_);
  
  if (success) {
    this->load_state_ = LoadState::LOADED;
    this->load_retry_count_ = 0;
  } else {
    this->load_state_ = LoadState::FAILED;
    this->load_retry_count_++;
  }
  
  return success;
}

void SdImageComponent::dump_config() {
  ESP_LOGCONFIG(TAG_IMAGE, "SD Image Component:");
  ESP_LOGCONFIG(TAG_IMAGE, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->image_width_, this->image_height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->format_to_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Loaded: %s", this->image_loaded_ ? "YES" : "NO");
  if (this->image_loaded_) {
    ESP_LOGCONFIG(TAG_IMAGE, "  Buffer size: %zu bytes", this->image_buffer_.size());
    ESP_LOGCONFIG(TAG_IMAGE, "  Base Image - W:%d H:%d Type:%d Data:%p", 
                  this->width_, this->height_, this->type_, this->data_start_);
  }
}

// REQUIRED VIRTUAL METHODS - These fix the linker error
int SdImageComponent::get_width() const {
  // CORRECTION: Auto-load pour les getters aussi
  if (!this->image_loaded_ && !const_cast<SdImageComponent*>(this)->ensure_loaded()) {
    return this->resize_width_ > 0 ? this->resize_width_ : 1;
  }
  return this->get_current_width();
}

int SdImageComponent::get_height() const {
  // CORRECTION: Auto-load pour les getters aussi
  if (!this->image_loaded_ && !const_cast<SdImageComponent*>(this)->ensure_loaded()) {
    return this->resize_height_ > 0 ? this->resize_height_ : 1;
  }
  return this->get_current_height();
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
  if (byte_order == "BIG_ENDIAN") {
    this->byte_order_ = SdByteOrder::BIG_ENDIAN_SD;
    ESP_LOGD(TAG_IMAGE, "Byte order set to: BIG_ENDIAN");
  } else {
    this->byte_order_ = SdByteOrder::LITTLE_ENDIAN_SD;
    ESP_LOGD(TAG_IMAGE, "Byte order set to: LITTLE_ENDIAN");
  }
}

// Implementation of draw() method according to ESPHome source code
void SdImageComponent::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  // CORRECTION: Auto-load intégré dans draw()
  if (!this->ensure_loaded()) {
    ESP_LOGW(TAG_IMAGE, "Cannot draw: failed to load image %s", this->file_path_.c_str());
    return;
  }
  
  ESP_LOGD(TAG_IMAGE, "Drawing SD image %dx%d at position %d,%d (Base: W:%d H:%d Data:%p)", 
           this->get_current_width(), this->get_current_height(), x, y,
           this->width_, this->height_, this->data_start_);
  
  // If base data is correct, use ESPHome's optimized method
  if (this->data_start_ && this->width_ > 0 && this->height_ > 0) {
    ESP_LOGD(TAG_IMAGE, "Using ESPHome base image draw method");
    // Call base method that handles clipping and optimization
    image::Image::draw(x, y, display, color_on, color_off);
  } else {
    ESP_LOGD(TAG_IMAGE, "Using fallback pixel-by-pixel drawing");
    // Fallback: draw pixel by pixel
    this->draw_pixels_directly(x, y, display, color_on, color_off);
  }
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
  
  // Finalize loading by updating base properties
  this->finalize_image_load();
  
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
  
  // Reset base class properties as well
  this->width_ = 0;
  this->height_ = 0;
  this->data_start_ = nullptr;
  this->bpp_ = 0;
  
  // Reset load state
  this->load_state_ = LoadState::NOT_LOADED;
  this->load_retry_count_ = 0;
}

bool SdImageComponent::reload_image() {
  std::string path = this->file_path_;
  this->unload_image();
  return this->load_image_from_path(path);
}

void SdImageComponent::finalize_image_load() {
  if (this->image_loaded_) {
    this->update_base_image_properties();
    ESP_LOGI(TAG_IMAGE, "Image properties updated - W:%d H:%d Type:%d Data:%p BPP:%d", 
             this->width_, this->height_, this->type_, this->data_start_, this->bpp_);
  }
}

void SdImageComponent::update_base_image_properties() {
  // Update base class members
  this->width_ = this->get_current_width();
  this->height_ = this->get_current_height();
  this->type_ = this->get_esphome_image_type();
  
  if (!this->image_buffer_.empty()) {
    this->data_start_ = this->image_buffer_.data();
    
    // Calculate bpp according to ESPHome source code
    switch (this->type_) {
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
  } else {
    this->data_start_ = nullptr;
    this->bpp_ = 0;
  }
}

int SdImageComponent::get_current_width() const {
  return this->resize_width_ > 0 ? this->resize_width_ : this->image_width_;
}

int SdImageComponent::get_current_height() const {
  return this->resize_height_ > 0 ? this->resize_height_ : this->image_height_;
}

image::ImageType SdImageComponent::get_esphome_image_type() const {
  switch (this->format_) {
    case ImageFormat::RGB565: return image::IMAGE_TYPE_RGB565;
    case ImageFormat::RGB888: return image::IMAGE_TYPE_RGB;
    case ImageFormat::RGBA: return image::IMAGE_TYPE_RGB; // ESPHome doesn't have native RGBA
    default: return image::IMAGE_TYPE_RGB565;
  }
}

void SdImageComponent::draw_pixels_directly(int x, int y, display::Display *display, Color color_on, Color color_off) {
  ESP_LOGD(TAG_IMAGE, "Drawing %dx%d pixels directly", this->get_current_width(), this->get_current_height());
  
  for (int img_y = 0; img_y < this->get_current_height(); img_y++) {
    for (int img_x = 0; img_x < this->get_current_width(); img_x++) {
      Color pixel_color = this->get_pixel_color(img_x, img_y);
      display->draw_pixel_at(x + img_x, y + img_y, pixel_color);
    }
    
    // Yield periodically to avoid watchdog
    if (img_y % 32 == 0) {
      App.feed_wdt();
      yield();
    }
  }
}

void SdImageComponent::draw_pixel_at(display::Display *display, int screen_x, int screen_y, int img_x, int img_y) {
  Color pixel_color = this->get_pixel_color(img_x, img_y);
  display->draw_pixel_at(screen_x, screen_y, pixel_color);
}

Color SdImageComponent::get_pixel_color(int x, int y) const {
  if (x < 0 || x >= this->get_current_width() || y < 0 || y >= this->get_current_height()) {
    return Color::BLACK;
  }
  
  size_t offset = (y * this->get_current_width() + x) * this->get_pixel_size();
  
  if (offset + this->get_pixel_size() > this->image_buffer_.size()) {
    return Color::BLACK;
  }
  
  switch (this->format_) {
    case ImageFormat::RGB565: {
      uint16_t rgb565;
      if (this->byte_order_ == SdByteOrder::BIG_ENDIAN_SD) {
        // Big endian: MSB en premier
        rgb565 = (this->image_buffer_[offset] << 8) | this->image_buffer_[offset + 1];
      } else {
        // Little endian: LSB en premier
        rgb565 = this->image_buffer_[offset] | (this->image_buffer_[offset + 1] << 8);
      }
      uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (rgb565 & 0x1F) << 3;
      return Color(r, g, b);
    }
    case ImageFormat::RGB888:
      return Color(this->image_buffer_[offset], 
                  this->image_buffer_[offset + 1], 
                  this->image_buffer_[offset + 2]);
    case ImageFormat::RGBA:
      return Color(this->image_buffer_[offset], 
                  this->image_buffer_[offset + 1], 
                  this->image_buffer_[offset + 2], 
                  this->image_buffer_[offset + 3]);
    default:
      return Color::BLACK;
  }
}

// File type detection
SdImageComponent::FileType SdImageComponent::detect_file_type(const std::vector<uint8_t> &data) const {
  if (this->is_jpeg_data(data)) return FileType::JPEG;
  if (this->is_png_data(data)) return FileType::PNG;
  if (this->is_gif_data(data)) return FileType::GIF;
  return FileType::UNKNOWN;
}

bool SdImageComponent::is_jpeg_data(const std::vector<uint8_t> &data) const {
  return data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool SdImageComponent::is_png_data(const std::vector<uint8_t> &data) const {
  // PNG signature: 89 50 4E 47 0D 0A 1A 0A
  return data.size() >= 8 && 
         data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
         data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A;
}

bool SdImageComponent::is_gif_data(const std::vector<uint8_t> &data) const {
  // GIF signature: GIF87a ou GIF89a
  return data.size() >= 6 && 
         data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
         ((data[3] == '8' && data[4] == '7' && data[5] == 'a') ||
          (data[3] == '8' && data[4] == '9' && data[5] == 'a'));
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
      
    case FileType::GIF:
      ESP_LOGI(TAG_IMAGE, "Decoding GIF image");
      return this->decode_gif_image(data);
      
    default:
      ESP_LOGE(TAG_IMAGE, "Unsupported image format (only JPEG, PNG and GIF supported)");
      return false;
  }
}

// =====================================================
// JPEG Decoder Implementation
// =====================================================

#ifdef USE_JPEGDEC

bool SdImageComponent::decode_jpeg_image(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGD(TAG_IMAGE, "Using JPEGDEC decoder with post-decode resize");
  
  current_image_component = this;
  
  this->jpeg_decoder_ = new JPEGDEC();
  if (!this->jpeg_decoder_) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate JPEG decoder");
    current_image_component = nullptr;
    return false;
  }
  
  int result = this->jpeg_decoder_->openRAM((uint8_t*)jpeg_data.data(), jpeg_data.size(), 
                                           SdImageComponent::jpeg_decode_callback_no_resize);
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to open JPEG data: %d", result);
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Get original dimensions
  int orig_width = this->jpeg_decoder_->getWidth();
  int orig_height = this->jpeg_decoder_->getHeight();
  
  ESP_LOGI(TAG_IMAGE, "JPEG original dimensions: %dx%d", orig_width, orig_height);
  
  // Validate dimensions
  if (orig_width <= 0 || orig_height <= 0 || 
      orig_width > 2048 || orig_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid JPEG dimensions: %dx%d", orig_width, orig_height);
    this->jpeg_decoder_->close();
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Temporarily set dimensions to original for decoding
  this->image_width_ = orig_width;
  this->image_height_ = orig_height;
  this->format_ = ImageFormat::RGB565;
  
  // Allocate temporary buffer for original size
  if (!this->allocate_image_buffer()) {
    this->jpeg_decoder_->close();
    delete this->jpeg_decoder_;
    this->jpeg_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "Decoding JPEG at original size...");
  
  // Decode at original size
  result = this->jpeg_decoder_->decode(0, 0, 0);
  
  this->jpeg_decoder_->close();
  delete this->jpeg_decoder_;
  this->jpeg_decoder_ = nullptr;
  current_image_component = nullptr;
  
  if (result != 1) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode JPEG: %d", result);
    return false;
  }
  
  // Now resize if needed
  if (this->resize_width_ > 0 && this->resize_height_ > 0 &&
      (this->resize_width_ != orig_width || this->resize_height_ != orig_height)) {
    
    ESP_LOGI(TAG_IMAGE, "Resizing JPEG from %dx%d to %dx%d", 
             orig_width, orig_height, this->resize_width_, this->resize_height_);
    
    if (!this->resize_image_buffer(orig_width, orig_height, 
                                  this->resize_width_, this->resize_height_)) {
      ESP_LOGE(TAG_IMAGE, "Failed to resize JPEG image");
      return false;
    }
    
    // Update dimensions
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
  }
  
  ESP_LOGI(TAG_IMAGE, "JPEG processed successfully: %dx%d", 
           this->image_width_, this->image_height_);
  
  return true;
}

// No-resize callback for original size decoding
int SdImageComponent::jpeg_decode_callback_no_resize(JPEGDRAW *pDraw) {
  if (!current_image_component || !pDraw || !pDraw->pPixels) {
    return 0;
  }
  
  SdImageComponent *component = current_image_component;
  uint16_t *pixels = (uint16_t *)pDraw->pPixels;
  
  // Direct copy without any resize logic
  for (int py = 0; py < pDraw->iHeight; py++) {
    for (int px = 0; px < pDraw->iWidth; px++) {
      int img_x = pDraw->x + px;
      int img_y = pDraw->y + py;
      
      if (img_x >= 0 && img_x < component->image_width_ && 
          img_y >= 0 && img_y < component->image_height_) {
        
        uint16_t rgb565 = pixels[py * pDraw->iWidth + px];
        size_t offset = (img_y * component->image_width_ + img_x) * 2;
        
        if (offset + 1 < component->image_buffer_.size()) {
          // Respect byte order configuration
          if (component->byte_order_ == SdByteOrder::BIG_ENDIAN_SD) {
            // Big endian: MSB first
            component->image_buffer_[offset] = (rgb565 >> 8) & 0xFF;
            component->image_buffer_[offset + 1] = rgb565 & 0xFF;
          } else {
            // Little endian: LSB first (default)
            component->image_buffer_[offset] = rgb565 & 0xFF;
            component->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
          }
        }
      }
    }
    
    if (py % 16 == 0) {
      App.feed_wdt();
      yield();
    }
  }
  
  return 1;
}

// Legacy callback with fixed resize logic (kept for compatibility)
int SdImageComponent::jpeg_decode_callback(JPEGDRAW *pDraw) {
  if (!current_image_component) {
    ESP_LOGE(TAG_IMAGE, "No current image component in callback");
    return 0;
  }
  
  SdImageComponent *component = current_image_component;
  
  if (!pDraw || !pDraw->pPixels) {
    ESP_LOGE(TAG_IMAGE, "Invalid draw parameters in callback");
    return 0;
  }
  
  // Get original JPEG dimensions
  int orig_width = component->jpeg_decoder_->getWidth();
  int orig_height = component->jpeg_decoder_->getHeight();
  
  // Check if we need to resize
  bool need_resize = (component->resize_width_ > 0 && component->resize_height_ > 0) &&
                    (component->resize_width_ != orig_width || component->resize_height_ != orig_height);
  
  uint16_t *pixels = (uint16_t *)pDraw->pPixels;
  
  if (need_resize) {
    // RESIZE MODE: Sample and scale pixels
    float scale_x = (float)orig_width / component->resize_width_;
    float scale_y = (float)orig_height / component->resize_height_;
    
    // For each pixel in the resized output that falls within this draw block
    for (int out_y = 0; out_y < component->resize_height_; out_y++) {
      for (int out_x = 0; out_x < component->resize_width_; out_x++) {
        // Calculate corresponding source pixel
        int src_x = (int)(out_x * scale_x);
        int src_y = (int)(out_y * scale_y);
        
        // Check if source pixel is in current draw block
        if (src_x >= pDraw->x && src_x < (pDraw->x + pDraw->iWidth) &&
            src_y >= pDraw->y && src_y < (pDraw->y + pDraw->iHeight)) {
          
          // Get pixel from draw block
          int block_x = src_x - pDraw->x;
          int block_y = src_y - pDraw->y;
          uint16_t rgb565 = pixels[block_y * pDraw->iWidth + block_x];
          
          // Store in resized buffer
          size_t offset = (out_y * component->resize_width_ + out_x) * 2;
          if (offset + 1 < component->image_buffer_.size()) {
            component->image_buffer_[offset] = rgb565 & 0xFF;
            component->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
          }
        }
      }
      
      if (out_y % 8 == 0) {
        App.feed_wdt();
        yield();
      }
    }
  } else {
    // NO RESIZE: Direct copy
    for (int py = 0; py < pDraw->iHeight; py++) {
      for (int px = 0; px < pDraw->iWidth; px++) {
        int img_x = pDraw->x + px;
        int img_y = pDraw->y + py;
        
        if (img_x >= 0 && img_x < component->image_width_ && 
            img_y >= 0 && img_y < component->image_height_) {
          
          uint16_t rgb565 = pixels[py * pDraw->iWidth + px];
          size_t offset = (img_y * component->image_width_ + img_x) * 2;
          
          if (offset + 1 < component->image_buffer_.size()) {
            component->image_buffer_[offset] = rgb565 & 0xFF;
            component->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
          }
        }
      }
      
      if (py % 16 == 0) {
        App.feed_wdt();
        yield();
      }
    }
  }
  
  return 1;
}

#else // !USE_JPEGDEC

bool SdImageComponent::decode_jpeg_image(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGE(TAG_IMAGE, "JPEG support not compiled in (USE_JPEGDEC not defined)");
  return false;
}

int SdImageComponent::jpeg_decode_callback(JPEGDRAW *pDraw) {
  return 0;
}

int SdImageComponent::jpeg_decode_callback_no_resize(JPEGDRAW *pDraw) {
  return 0;
}

#endif // USE_JPEGDEC

// =====================================================
// PNG Decoder Implementation
// =====================================================

#ifdef USE_PNGLE

bool SdImageComponent::decode_png_image(const std::vector<uint8_t> &png_data) {
  ESP_LOGD(TAG_IMAGE, "Using PNGLE decoder");
  
  current_image_component = this;
  
  this->png_decoder_ = pngle_new();
  if (!this->png_decoder_) {
    ESP_LOGE(TAG_IMAGE, "Failed to create PNG decoder");
    current_image_component = nullptr;
    return false;
  }
  
  // Set callbacks based on resize requirements
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    pngle_set_init_callback(this->png_decoder_, SdImageComponent::png_init_callback);
    pngle_set_draw_callback(this->png_decoder_, SdImageComponent::png_draw_callback);
  } else {
    pngle_set_init_callback(this->png_decoder_, SdImageComponent::png_init_callback_no_resize);
    pngle_set_draw_callback(this->png_decoder_, SdImageComponent::png_draw_callback_no_resize);
  }
  
  pngle_set_done_callback(this->png_decoder_, SdImageComponent::png_done_callback);
  
  // Feed data to decoder
  int result = pngle_feed(this->png_decoder_, png_data.data(), png_data.size());
  
  pngle_destroy(this->png_decoder_);
  this->png_decoder_ = nullptr;
  current_image_component = nullptr;
  
  if (result < 0) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode PNG: %d", result);
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "PNG decoded successfully: %dx%d", 
           this->image_width_, this->image_height_);
  
  return true;
}

void SdImageComponent::png_init_callback(pngle_t *pngle, uint32_t w, uint32_t h) {
  if (!current_image_component) return;
  
  SdImageComponent *component = current_image_component;
  
  ESP_LOGI(TAG_IMAGE, "PNG original dimensions: %dx%d", w, h);
  ESP_LOGI(TAG_IMAGE, "PNG target dimensions: %dx%d", component->resize_width_, component->resize_height_);
  
  // Set to target dimensions
  component->image_width_ = component->resize_width_;
  component->image_height_ = component->resize_height_;
  component->format_ = ImageFormat::RGB565;
  
  if (!component->allocate_image_buffer()) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate PNG buffer");
  }
}

void SdImageComponent::png_draw_callback(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  if (!current_image_component) return;
  
  SdImageComponent *component = current_image_component;
  
  // Get original PNG dimensions
  uint32_t orig_width = pngle_get_width(pngle);
  uint32_t orig_height = pngle_get_height(pngle);
  
  // Calculate scaling factors
  float scale_x = (float)component->resize_width_ / orig_width;
  float scale_y = (float)component->resize_height_ / orig_height;
  
  // Map original coordinates to resized coordinates
  int dst_x = (int)(x * scale_x);
  int dst_y = (int)(y * scale_y);
  
  // Only process if within bounds
  if (dst_x >= 0 && dst_x < component->resize_width_ && 
      dst_y >= 0 && dst_y < component->resize_height_) {
    component->set_pixel(dst_x, dst_y, rgba[0], rgba[1], rgba[2], rgba[3]);
  }
}

void SdImageComponent::png_done_callback(pngle_t *pngle) {
  if (!current_image_component) return;
  ESP_LOGD(TAG_IMAGE, "PNG decoding completed");
}

void SdImageComponent::png_init_callback_no_resize(pngle_t *pngle, uint32_t w, uint32_t h) {
  if (!current_image_component) return;
  
  SdImageComponent *component = current_image_component;
  
  ESP_LOGI(TAG_IMAGE, "PNG dimensions: %dx%d (no resize)", w, h);
  
  // Validate dimensions
  if (w <= 0 || h <= 0 || w > 2048 || h > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid PNG dimensions: %dx%d", w, h);
    return;
  }
  
  // Set actual dimensions
  component->image_width_ = w;
  component->image_height_ = h;
  component->format_ = ImageFormat::RGB565;
  
  if (!component->allocate_image_buffer()) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate PNG buffer");
  }
}

void SdImageComponent::png_draw_callback_no_resize(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  if (!current_image_component) return;
  
  SdImageComponent *component = current_image_component;
  
  // Direct pixel placement without resize
  if (x >= 0 && x < (uint32_t)component->image_width_ && 
      y >= 0 && y < (uint32_t)component->image_height_) {
    component->set_pixel(x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
  }
}

#else // !USE_PNGLE

bool SdImageComponent::decode_png_image(const std::vector<uint8_t> &png_data) {
  ESP_LOGE(TAG_IMAGE, "PNG support not compiled in (USE_PNGLE not defined)");
  return false;
}

// Empty callback stubs with correct signatures
void SdImageComponent::png_init_callback(pngle_t *pngle, uint32_t w, uint32_t h) {
  // Empty stub
}

void SdImageComponent::png_draw_callback(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  // Empty stub
}

void SdImageComponent::png_done_callback(pngle_t *pngle) {
  // Empty stub
}

void SdImageComponent::png_init_callback_no_resize(pngle_t *pngle, uint32_t w, uint32_t h) {
  // Empty stub
}

void SdImageComponent::png_draw_callback_no_resize(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  // Empty stub
}

#endif // USE_PNGLE

// =====================================================
// GIF Decoder Implementation
// =====================================================

#ifdef USE_ANIMATEDGIF

bool SdImageComponent::decode_gif_image(const std::vector<uint8_t> &gif_data) {
  ESP_LOGD(TAG_IMAGE, "Using AnimatedGIF decoder for first frame");
  
  current_image_component = this;
  
  this->gif_decoder_ = new ANIMATEDGIF();
  if (!this->gif_decoder_) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate GIF decoder");
    current_image_component = nullptr;
    return false;
  }
  
  int result = this->gif_decoder_->open((uint8_t*)gif_data.data(), gif_data.size(), GIFDraw);
  if (result != GIF_SUCCESS) {
    ESP_LOGE(TAG_IMAGE, "Failed to open GIF data: %d", result);
    delete this->gif_decoder_;
    this->gif_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Get GIF info
  GIFINFO gif_info;
  this->gif_decoder_->getInfo(&gif_info);
  
  ESP_LOGI(TAG_IMAGE, "GIF info: %dx%d, %d frames", gif_info.iWidth, gif_info.iHeight, gif_info.iFrameCount);
  
  // Validate dimensions
  if (gif_info.iWidth <= 0 || gif_info.iHeight <= 0 || 
      gif_info.iWidth > 2048 || gif_info.iHeight > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid GIF dimensions: %dx%d", gif_info.iWidth, gif_info.iHeight);
    this->gif_decoder_->close();
    delete this->gif_decoder_;
    this->gif_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  // Set dimensions and format
  this->image_width_ = gif_info.iWidth;
  this->image_height_ = gif_info.iHeight;
  this->format_ = ImageFormat::RGB565;
  
  // Allocate buffer
  if (!this->allocate_image_buffer()) {
    this->gif_decoder_->close();
    delete this->gif_decoder_;
    this->gif_decoder_ = nullptr;
    current_image_component = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG_IMAGE, "Decoding first frame of GIF...");
  
  // Decode first frame
  result = this->gif_decoder_->playFrame(true, nullptr);
  
  this->gif_decoder_->close();
  delete this->gif_decoder_;
  this->gif_decoder_ = nullptr;
  current_image_component = nullptr;
  
  if (result != GIF_SUCCESS) {
    ESP_LOGE(TAG_IMAGE, "Failed to decode GIF frame: %d", result);
    return false;
  }
  
  // Apply resize if needed
  int orig_width = this->image_width_;
  int orig_height = this->image_height_;
  
  if (this->resize_width_ > 0 && this->resize_height_ > 0 &&
      (this->resize_width_ != orig_width || this->resize_height_ != orig_height)) {
    
    ESP_LOGI(TAG_IMAGE, "Resizing GIF from %dx%d to %dx%d", 
             orig_width, orig_height, this->resize_width_, this->resize_height_);
    
    if (!this->resize_image_buffer(orig_width, orig_height, 
                                  this->resize_width_, this->resize_height_)) {
      ESP_LOGE(TAG_IMAGE, "Failed to resize GIF image");
      return false;
    }
    
    this->image_width_ = this->resize_width_;
    this->image_height_ = this->resize_height_;
  }
  
  ESP_LOGI(TAG_IMAGE, "GIF processed successfully: %dx%d", 
           this->image_width_, this->image_height_);
  
  return true;
}

// GIF draw callback
void SdImageComponent::GIFDraw(GIFDRAW *pDraw) {
  if (!current_image_component || !pDraw || !pDraw->pPixels) {
    return;
  }
  
  SdImageComponent *component = current_image_component;
  uint16_t *pixels = (uint16_t *)pDraw->pPixels;
  
  // Convert from GIF palette to RGB565 and store
  for (int py = 0; py < pDraw->iHeight; py++) {
    for (int px = 0; px < pDraw->iWidth; px++) {
      int img_x = pDraw->iX + px;
      int img_y = pDraw->iY + py;
      
      if (img_x >= 0 && img_x < component->image_width_ && 
          img_y >= 0 && img_y < component->image_height_) {
        
        uint16_t rgb565 = pixels[py * pDraw->iWidth + px];
        size_t offset = (img_y * component->image_width_ + img_x) * 2;
        
        if (offset + 1 < component->image_buffer_.size()) {
          if (component->byte_order_ == SdByteOrder::BIG_ENDIAN_SD) {
            component->image_buffer_[offset] = (rgb565 >> 8) & 0xFF;
            component->image_buffer_[offset + 1] = rgb565 & 0xFF;
          } else {
            component->image_buffer_[offset] = rgb565 & 0xFF;
            component->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
          }
        }
      }
    }
    
    if (py % 16 == 0) {
      App.feed_wdt();
      yield();
    }
  }
}

#else // !USE_ANIMATEDGIF

bool SdImageComponent::decode_gif_image(const std::vector<uint8_t> &gif_data) {
  ESP_LOGE(TAG_IMAGE, "GIF support not compiled in (USE_ANIMATEDGIF not defined)");
  return false;
}

void SdImageComponent::GIFDraw(GIFDRAW *pDraw) {
  // Empty stub
}

#endif // USE_ANIMATEDGIF

// =====================================================
// Resize Methods Implementation
// =====================================================

bool SdImageComponent::resize_image_buffer(int src_width, int src_height, int dst_width, int dst_height) {
  if (this->image_buffer_.empty()) {
    ESP_LOGE(TAG_IMAGE, "Source buffer is empty");
    return false;
  }
  
  if (dst_width <= 0 || dst_height <= 0 || dst_width > 2048 || dst_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid resize dimensions: %dx%d", dst_width, dst_height);
    return false;
  }
  
  // Create new buffer for resized image
  std::vector<uint8_t> new_buffer(dst_width * dst_height * 2); // RGB565
  
  // Simple nearest-neighbor resize
  float scale_x = (float)src_width / dst_width;
  float scale_y = (float)src_height / dst_height;
  
  ESP_LOGI(TAG_IMAGE, "Resizing %dx%d -> %dx%d (scale: %.3f, %.3f)", 
           src_width, src_height, dst_width, dst_height, scale_x, scale_y);
  
  for (int dst_y = 0; dst_y < dst_height; dst_y++) {
    for (int dst_x = 0; dst_x < dst_width; dst_x++) {
      
      // Find corresponding source pixel
      int src_x = (int)(dst_x * scale_x);
      int src_y = (int)(dst_y * scale_y);
      
      // Clamp to source bounds
      if (src_x >= src_width) src_x = src_width - 1;
      if (src_y >= src_height) src_y = src_height - 1;
      
      // Copy pixel
      size_t src_offset = (src_y * src_width + src_x) * 2;
      size_t dst_offset = (dst_y * dst_width + dst_x) * 2;
      
      if (src_offset + 1 < this->image_buffer_.size() && 
          dst_offset + 1 < new_buffer.size()) {
        new_buffer[dst_offset] = this->image_buffer_[src_offset];
        new_buffer[dst_offset + 1] = this->image_buffer_[src_offset + 1];
      }
    }
    
    // Yield periodically
    if (dst_y % 32 == 0) {
      App.feed_wdt();
      yield();
    }
  }
  
  // Replace buffer
  this->image_buffer_ = std::move(new_buffer);
  
  ESP_LOGI(TAG_IMAGE, "Image resized successfully from %dx%d to %dx%d", 
           src_width, src_height, dst_width, dst_height);
  
  return true;
}

bool SdImageComponent::resize_image_buffer_bilinear(int src_width, int src_height, int dst_width, int dst_height) {
  if (this->image_buffer_.empty()) {
    ESP_LOGE(TAG_IMAGE, "Source buffer is empty");
    return false;
  }
  
  if (dst_width <= 0 || dst_height <= 0 || dst_width > 2048 || dst_height > 2048) {
    ESP_LOGE(TAG_IMAGE, "Invalid resize dimensions: %dx%d", dst_width, dst_height);
    return false;
  }
  
  std::vector<uint8_t> new_buffer(dst_width * dst_height * 2);
  
  float scale_x = (float)(src_width - 1) / (dst_width - 1);
  float scale_y = (float)(src_height - 1) / (dst_height - 1);
  
  ESP_LOGI(TAG_IMAGE, "Bilinear resizing %dx%d -> %dx%d", src_width, src_height, dst_width, dst_height);
  
  for (int dst_y = 0; dst_y < dst_height; dst_y++) {
    for (int dst_x = 0; dst_x < dst_width; dst_x++) {
      
      float src_x_f = dst_x * scale_x;
      float src_y_f = dst_y * scale_y;
      
      int src_x0 = (int)src_x_f;
      int src_y0 = (int)src_y_f;
      int src_x1 = std::min(src_x0 + 1, src_width - 1);
      int src_y1 = std::min(src_y0 + 1, src_height - 1);
      
      float dx = src_x_f - src_x0;
      float dy = src_y_f - src_y0;
      
      // Get 4 surrounding pixels
      auto get_pixel = [&](int x, int y) -> uint16_t {
        size_t offset = (y * src_width + x) * 2;
        if (offset + 1 < this->image_buffer_.size()) {
          return this->image_buffer_[offset] | (this->image_buffer_[offset + 1] << 8);
        }
        return 0;
      };
      
      uint16_t p00 = get_pixel(src_x0, src_y0);
      uint16_t p01 = get_pixel(src_x0, src_y1);
      uint16_t p10 = get_pixel(src_x1, src_y0);
      uint16_t p11 = get_pixel(src_x1, src_y1);
      
      // Interpolate RGB components separately
      auto interpolate_rgb565 = [](uint16_t p00, uint16_t p01, uint16_t p10, uint16_t p11, 
                                  float dx, float dy) -> uint16_t {
        // Extract RGB components
        uint8_t r00 = (p00 >> 11) & 0x1F, g00 = (p00 >> 5) & 0x3F, b00 = p00 & 0x1F;
        uint8_t r01 = (p01 >> 11) & 0x1F, g01 = (p01 >> 5) & 0x3F, b01 = p01 & 0x1F;
        uint8_t r10 = (p10 >> 11) & 0x1F, g10 = (p10 >> 5) & 0x3F, b10 = p10 & 0x1F;
        uint8_t r11 = (p11 >> 11) & 0x1F, g11 = (p11 >> 5) & 0x3F, b11 = p11 & 0x1F;
        
        // Bilinear interpolation
        float r = r00 * (1-dx) * (1-dy) + r10 * dx * (1-dy) + r01 * (1-dx) * dy + r11 * dx * dy;
        float g = g00 * (1-dx) * (1-dy) + g10 * dx * (1-dy) + g01 * (1-dx) * dy + g11 * dx * dy;
        float b = b00 * (1-dx) * (1-dy) + b10 * dx * (1-dy) + b01 * (1-dx) * dy + b11 * dx * dy;
        
        return ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;
      };
      
      uint16_t result = interpolate_rgb565(p00, p01, p10, p11, dx, dy);
      
      size_t dst_offset = (dst_y * dst_width + dst_x) * 2;
      if (dst_offset + 1 < new_buffer.size()) {
        new_buffer[dst_offset] = result & 0xFF;
        new_buffer[dst_offset + 1] = (result >> 8) & 0xFF;
      }
    }
    
    if (dst_y % 16 == 0) {
      App.feed_wdt();
      yield();
    }
  }
  
  this->image_buffer_ = std::move(new_buffer);
  
  ESP_LOGI(TAG_IMAGE, "Image resized with bilinear interpolation from %dx%d to %dx%d", 
           src_width, src_height, dst_width, dst_height);
  
  return true;
}

// =====================================================
// Helper Methods Implementation
// =====================================================

bool SdImageComponent::allocate_image_buffer() {
  size_t buffer_size = this->get_buffer_size();
  
  if (buffer_size == 0 || buffer_size > 16 * 1024 * 1024) {
    ESP_LOGE(TAG_IMAGE, "Invalid buffer size: %zu bytes", buffer_size);
    return false;
  }
  
  this->image_buffer_.clear();
  
  // Use reserve and resize without try-catch since exceptions are disabled
  this->image_buffer_.reserve(buffer_size);
  if (this->image_buffer_.capacity() < buffer_size) {
    ESP_LOGE(TAG_IMAGE, "Failed to reserve %zu bytes for image buffer", buffer_size);
    return false;
  }
  
  this->image_buffer_.resize(buffer_size, 0);
  if (this->image_buffer_.size() != buffer_size) {
    ESP_LOGE(TAG_IMAGE, "Failed to allocate %zu bytes for image buffer", buffer_size);
    this->image_buffer_.clear();
    return false;
  }
  
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
      
      if (this->byte_order_ == SdByteOrder::BIG_ENDIAN_SD) {
        // Big endian: MSB en premier
        this->image_buffer_[offset] = (rgb565 >> 8) & 0xFF;
        this->image_buffer_[offset + 1] = rgb565 & 0xFF;
      } else {
        // Little endian: LSB en premier (défaut)
        this->image_buffer_[offset] = rgb565 & 0xFF;
        this->image_buffer_[offset + 1] = (rgb565 >> 8) & 0xFF;
      }
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
        ESP_LOGI(TAG_IMAGE, "  File: %s (%ld bytes)", entry->d_name, (long)st.st_size);
        file_count++;
      } else if (S_ISDIR(st.st_mode)) {
        ESP_LOGI(TAG_IMAGE, "  Dir: %s/", entry->d_name);
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

bool SdImageComponent::jpeg_decode_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  // Apply resize scaling if needed
  if (this->resize_width_ > 0 && this->resize_height_ > 0) {
    int orig_width = this->jpeg_decoder_->getWidth();
    int orig_height = this->jpeg_decoder_->getHeight();
    if (orig_width > 0 && orig_height > 0) {
      x = (x * this->resize_width_) / orig_width;
      y = (y * this->resize_height_) / orig_height;
    }
  }
  
  // Bounds check
  if (x < 0 || x >= this->image_width_ || y < 0 || y >= this->image_height_) {
    return false;
  }
  
  this->set_pixel(x, y, r, g, b);
  return true;
}

}  // namespace storage
}  // namespace esphome












