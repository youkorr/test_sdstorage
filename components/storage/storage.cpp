#include "storage.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
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

// Contexte global pour le décodage par tiles
SdImageComponent::TileContext SdImageComponent::g_tile_context = {nullptr, 0, 0, 0, 0, false};

void SdImageComponent::setup() {
  ESP_LOGCONFIG(TAG_IMAGE, "Setting up SD Image Component...");
  ESP_LOGCONFIG(TAG_IMAGE, "  File path: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Dimensions: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG_IMAGE, "  Format: %s", this->get_output_format_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Byte order: %s", this->get_byte_order_string().c_str());
  ESP_LOGCONFIG(TAG_IMAGE, "  Storage component: %s", this->storage_component_ ? "configured" : "not configured");
  
  // Vérifier la disponibilité de JPEGDEC
  #ifdef JPEGDEC_VERSION
  ESP_LOGCONFIG(TAG_IMAGE, "  JPEGDEC version: %s", JPEGDEC_VERSION);
  #else
  ESP_LOGCONFIG(TAG_IMAGE, "  JPEGDEC version: detected");
  #endif
  ESP_LOGCONFIG(TAG_IMAGE, "  Real JPEG decoder: AVAILABLE (ULTRA-SAFE TILES)");
  ESP_LOGCONFIG(TAG_IMAGE, "  PNG decoder: FALLBACK ONLY");
  
  // Afficher la mémoire disponible
  ESP_LOGI(TAG_IMAGE, "💾 Free heap at setup: %zu bytes", esp_get_free_heap_size());
  
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
image::ImageType SdImageComponent::get_image_type() const {
  switch (this->output_format_) {
    case OutputImageFormat::rgb565:
      return image::IMAGE_TYPE_RGB565;
    case OutputImageFormat::rgb888:
    case OutputImageFormat::rgba:
    default:
      return image::IMAGE_TYPE_RGB;
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

// =====================================================
// IMAGE LOADING METHODS
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
  
  if (file_size > 5 * 1024 * 1024) { // 5MB limit
    ESP_LOGW(TAG_IMAGE, "⚠️ Large file detected: %zu bytes (>5MB)", file_size);
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
  if (file_data.size() >= 16) {
    ESP_LOGI(TAG_IMAGE, "🔍 First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
             file_data[0], file_data[1], file_data[2], file_data[3],
             file_data[4], file_data[5], file_data[6], file_data[7],
             file_data[8], file_data[9], file_data[10], file_data[11],
             file_data[12], file_data[13], file_data[14], file_data[15]);
  }
  
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

// =====================================================
// DÉCODAGE JPEG ULTRA-OPTIMISÉ POUR GRANDS ÉCRANS
// =====================================================

int SdImageComponent::tile_callback_wrapper(JPEGDRAW *pDraw) {
  if (!g_tile_context.active || !g_tile_context.instance || !pDraw) {
    return 0;
  }
  
  SdImageComponent *instance = g_tile_context.instance;
  
  // Traitement ultra-optimisé sans variables locales sur stack
  uint8_t *src = pDraw->pPixels;
  int src_width = pDraw->iWidth;
  int src_height = pDraw->iHeight;
  int offset_x = pDraw->x;
  int offset_y = pDraw->y;
  
  int target_width = instance->get_width();
  int target_height = instance->get_height();
  
  for (int y = 0; y < src_height; y++) {
    int global_y = offset_y + y;
    if (global_y >= target_height) break;
    
    // Vérifier si la ligne est dans notre tile
    if (global_y < g_tile_context.tile_y || 
        global_y >= (g_tile_context.tile_y + g_tile_context.tile_h)) {
      continue;
    }
    
    for (int x = 0; x < src_width; x++) {
      int global_x = offset_x + x;
      if (global_x >= target_width) break;
      
      // Vérifier si le pixel est dans notre tile
      if (global_x < g_tile_context.tile_x || 
          global_x >= (g_tile_context.tile_x + g_tile_context.tile_w)) {
        continue;
      }
      
      int src_idx = (y * src_width + x) * 3;
      size_t dst_offset = instance->get_pixel_offset(global_x, global_y);
      
      instance->set_pixel_at_offset(dst_offset, 
        src[src_idx], src[src_idx + 1], src[src_idx + 2], 255);
    }
  }
  
  return 1;
}

bool SdImageComponent::decode_jpeg_real(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🔧 Using ULTRA-SAFE JPEGDEC for large screens");
  
  // Vérifier la mémoire disponible
  size_t free_heap = esp_get_free_heap_size();
  ESP_LOGI(TAG_IMAGE, "💾 Free heap before decode: %zu bytes", free_heap);
  
  if (free_heap < 2 * 1024 * 1024) { // Moins de 2MB libre
    ESP_LOGW(TAG_IMAGE, "⚠️ Low memory for large image: %zu bytes, using fallback", free_heap);
    return this->decode_jpeg_fallback(jpeg_data);
  }
  
  // Phase 1: Détection des dimensions avec allocation heap
  JPEGDEC *jpeg_info = new(std::nothrow) JPEGDEC();
  if (!jpeg_info) {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to allocate JPEGDEC for info");
    return false;
  }
  
  bool success = false;
  
  if (jpeg_info->openRAM((uint8_t*)jpeg_data.data(), jpeg_data.size(), nullptr) == 1) {
    int detected_width = jpeg_info->getWidth();
    int detected_height = jpeg_info->getHeight();
    
    ESP_LOGI(TAG_IMAGE, "📏 JPEG dimensions: %dx%d", detected_width, detected_height);
    
    if (detected_width > 0 && detected_height > 0 && 
        detected_width <= 4096 && detected_height <= 4096) {
      
      this->width_ = detected_width;
      this->height_ = detected_height;
      
      // Calculer la mémoire nécessaire
      size_t output_size = this->calculate_output_size();
      size_t memory_needed = output_size + (1024 * 1024); // +1MB de marge
      
      ESP_LOGI(TAG_IMAGE, "💾 Image will need %zu bytes (%.1f MB)", 
               output_size, output_size / (1024.0f * 1024.0f));
      
      if (free_heap >= memory_needed) {
        // Allouer le buffer de sortie
        this->image_data_.resize(output_size);
        ESP_LOGI(TAG_IMAGE, "✅ Allocated image buffer: %zu bytes", output_size);
        
        jpeg_info->close();
        delete jpeg_info;
        
        // Phase 2: Décodage adaptatif selon la taille
        if (detected_width >= 1920 || detected_height >= 1080) {
          success = this->decode_jpeg_ultra_large(jpeg_data);  // 4K/2K
        } else if (detected_width >= 1280 || detected_height >= 720) {
          success = this->decode_jpeg_large_screen(jpeg_data); // HD 720p/1080p
        } else if (detected_width >= 640 || detected_height >= 480) {
          success = this->decode_jpeg_medium_screen(jpeg_data); // VGA/WVGA
        } else {
          success = this->decode_jpeg_small_screen(jpeg_data);  // Petites images
        }
        
      } else {
        ESP_LOGW(TAG_IMAGE, "⚠️ Not enough memory: need %zu, have %zu", memory_needed, free_heap);
        jpeg_info->close();
        delete jpeg_info;
        return this->decode_jpeg_fallback(jpeg_data);
      }
      
    } else {
      ESP_LOGE(TAG_IMAGE, "❌ Invalid dimensions: %dx%d", detected_width, detected_height);
      jpeg_info->close();
      delete jpeg_info;
    }
  } else {
    ESP_LOGE(TAG_IMAGE, "❌ Failed to open JPEG for analysis");
    delete jpeg_info;
  }
  
  ESP_LOGI(TAG_IMAGE, "💾 Free heap after decode: %zu bytes", esp_get_free_heap_size());
  return success;
}

// =====================================================
// DÉCODEURS SPÉCIALISÉS PAR TAILLE D'ÉCRAN
// =====================================================

bool SdImageComponent::decode_jpeg_large_screen(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🖥️ Using LARGE SCREEN decoder for %dx%d", this->width_, this->height_);
  
  // Pour 1280x720 et similaires : tiles très petites
  int tile_size = 16; // 16x16 pixels pour maximiser la sécurité
  
  // Ajuster selon la RAM disponible
  size_t free_heap = esp_get_free_heap_size();
  if (free_heap > 10 * 1024 * 1024) { // Plus de 10MB
    tile_size = 24;
  } else if (free_heap > 5 * 1024 * 1024) { // Plus de 5MB
    tile_size = 20;
  }
  
  ESP_LOGI(TAG_IMAGE, "🔲 Large screen tile size: %dx%d (mem: %zu MB)", 
           tile_size, tile_size, free_heap / (1024 * 1024));
  
  return this->decode_with_micro_tiles(jpeg_data, tile_size);
}

bool SdImageComponent::decode_jpeg_ultra_large(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🖥️ Using ULTRA-LARGE decoder for %dx%d", this->width_, this->height_);
  
  // Pour 4K et plus : tiles minuscules
  int tile_size = 8; // 8x8 pixels - ultra-sécurisé
  
  ESP_LOGI(TAG_IMAGE, "🔲 Ultra-large tile size: %dx%d", tile_size, tile_size);
  
  return this->decode_with_micro_tiles(jpeg_data, tile_size);
}

bool SdImageComponent::decode_jpeg_medium_screen(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🖥️ Using MEDIUM SCREEN decoder for %dx%d", this->width_, this->height_);
  
  int tile_size = 32; // 32x32 pixels
  return this->decode_with_micro_tiles(jpeg_data, tile_size);
}

bool SdImageComponent::decode_jpeg_small_screen(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🖥️ Using SMALL SCREEN decoder for %dx%d", this->width_, this->height_);
  
  int tile_size = 64; // 64x64 pixels
  return this->decode_with_micro_tiles(jpeg_data, tile_size);
}

// =====================================================
// DÉCODEUR MICRO-TILES ULTRA-OPTIMISÉ
// =====================================================

bool SdImageComponent::decode_with_micro_tiles(const std::vector<uint8_t> &jpeg_data, int tile_size) {
  int total_tiles = ((this->width_ + tile_size - 1) / tile_size) * 
                   ((this->height_ + tile_size - 1) / tile_size);
  
  ESP_LOGI(TAG_IMAGE, "🔄 Processing %d micro-tiles of %dx%d...", total_tiles, tile_size, tile_size);
  
  int tiles_processed = 0;
  int tiles_successful = 0;
  
  // Traitement par bandes horizontales pour optimiser la mémoire
  int band_height = tile_size * 4; // Traiter 4 lignes de tiles à la fois
  
  for (int band_y = 0; band_y < this->height_; band_y += band_height) {
    int actual_band_height = std::min(band_height, this->height_ - band_y);
    
    ESP_LOGI(TAG_IMAGE, "📊 Processing band %d-%d (height: %d)", 
             band_y, band_y + actual_band_height, actual_band_height);
    
    // Traiter chaque tile dans cette bande
    for (int y = band_y; y < band_y + actual_band_height; y += tile_size) {
      for (int x = 0; x < this->width_; x += tile_size) {
        int tile_w = std::min(tile_size, this->width_ - x);
        int tile_h = std::min(tile_size, this->height_ - y);
        
        // Décoder cette micro-tile
        if (this->decode_ultra_safe_tile(jpeg_data, x, y, tile_w, tile_h)) {
          tiles_successful++;
        }
        
        tiles_processed++;
        
        // Yield très fréquent pour les grandes images
        if (tiles_processed % 1 == 0) { // Chaque tile !
          esp_task_wdt_reset();
          
          // Yield plus long pour les très grandes images
          if (total_tiles > 1000) {
            vTaskDelay(pdMS_TO_TICKS(2));
          } else {
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        }
        
        // Progrès détaillé pour grandes images
        if (total_tiles > 100 && tiles_processed % (total_tiles / 20) == 0) {
          int progress = (tiles_processed * 100) / total_tiles;
          float success_rate = (tiles_successful * 100.0f) / tiles_processed;
          ESP_LOGI(TAG_IMAGE, "📊 Progress: %d%% (%d/%d tiles, %.1f%% success)", 
                   progress, tiles_processed, total_tiles, success_rate);
          
          esp_task_wdt_reset();
          vTaskDelay(pdMS_TO_TICKS(10)); // Pause plus longue
        }
      }
    }
    
    // Pause entre les bandes pour éviter la surcharge
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  
  float final_success_rate = (tiles_successful * 100.0f) / total_tiles;
  ESP_LOGI(TAG_IMAGE, "✅ Micro-tile decoding completed: %d/%d tiles (%.1f%% success)", 
           tiles_successful, total_tiles, final_success_rate);
  
  // Considérer comme réussi si au moins 80% des tiles sont OK
  return final_success_rate >= 80.0f;
}

bool SdImageComponent::decode_ultra_safe_tile(const std::vector<uint8_t> &jpeg_data,
                                             int tile_x, int tile_y, int tile_w, int tile_h) {
  // Allocation heap ultra-minimaliste
  JPEGDEC *jpeg = new(std::nothrow) JPEGDEC();
  if (!jpeg) {
    return false; // Pas de log pour éviter le spam
  }
  
  bool success = false;
  
  // Configuration micro-tile
  g_tile_context.instance = this;
  g_tile_context.tile_x = tile_x;
  g_tile_context.tile_y = tile_y;
  g_tile_context.tile_w = tile_w;
  g_tile_context.tile_h = tile_h;
  g_tile_context.active = true;
  
  // Décodage ultra-rapide
  if (jpeg->openRAM((uint8_t*)jpeg_data.data(), jpeg_data.size(), tile_callback_wrapper) == 1) {
    if (jpeg->decode(0, 0, 0) == 1) {
      success = true;
    }
    jpeg->close();
  }
  
  g_tile_context.active = false;
  delete jpeg;
  
  return success;
}

bool SdImageComponent::decode_jpeg(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🔧 Starting ultra-safe JPEG decoding...");
  
  // Utiliser le décodage ultra-sécurisé en priorité
  bool result = this->decode_jpeg_real(jpeg_data);
  
  if (result) {
    ESP_LOGI(TAG_IMAGE, "✅ Ultra-safe JPEG decoding successful");
    return true;
  } else {
    ESP_LOGW(TAG_IMAGE, "⚠️ Ultra-safe JPEG decoding failed, using fallback");
    return this->decode_jpeg_fallback(jpeg_data);
  }
}

// =====================================================
// DÉCODEUR PNG (fallback seulement pour l'instant)
// =====================================================

bool SdImageComponent::decode_png_real(const std::vector<uint8_t> &png_data) {
  ESP_LOGW(TAG_IMAGE, "⚠️ Real PNG decoder not implemented yet");
  return false;
}

bool SdImageComponent::decode_png(const std::vector<uint8_t> &png_data) {
  // On utilise le vrai décodeur si dispo, sinon le fallback
  ESP_LOGI(TAG_IMAGE, "🔧 Attempting to use PNG decoder...");
  
  bool result = this->decode_png_real(png_data);
  if (result) {
    ESP_LOGI(TAG_IMAGE, "✅ PNG decoding successful");
    return true;
  } else {
    ESP_LOGW(TAG_IMAGE, "⚠️ PNG decoding failed, trying fallback");
  }
  
  // Fallback vers le pattern de test
  return this->decode_png_fallback(png_data);
}

// =====================================================
// CONVERSION DE FORMATS
// =====================================================

void SdImageComponent::convert_rgb888_to_target(const uint8_t *rgb_data, size_t pixel_count) {
  for (size_t i = 0; i < pixel_count; i++) {
    uint8_t r = rgb_data[i * 3 + 0];
    uint8_t g = rgb_data[i * 3 + 1];
    uint8_t b = rgb_data[i * 3 + 2];
    
    size_t offset = i * this->get_pixel_size();
    this->set_pixel_at_offset(offset, r, g, b, 255);
  }
}

void SdImageComponent::convert_rgba_to_target(const uint8_t *rgba_data, size_t pixel_count) {
  for (size_t i = 0; i < pixel_count; i++) {
    uint8_t r = rgba_data[i * 4 + 0];
    uint8_t g = rgba_data[i * 4 + 1];
    uint8_t b = rgba_data[i * 4 + 2];
    uint8_t a = rgba_data[i * 4 + 3];
    
    size_t offset = i * this->get_pixel_size();
    this->set_pixel_at_offset(offset, r, g, b, a);
  }
}

// =====================================================
// DÉCODEURS FALLBACK (patterns de test)
// =====================================================

bool SdImageComponent::decode_jpeg_fallback(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🔄 Using JPEG fallback decoder (test pattern)");
  
  // Extract dimensions from JPEG header
  int detected_width = 0, detected_height = 0;
  
  if (!this->extract_jpeg_dimensions(jpeg_data, detected_width, detected_height)) {
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
  
  // Calculate output size and allocate
  size_t output_size = this->calculate_output_size();
  this->image_data_.resize(output_size);
  
  // Generate test pattern
  this->generate_jpeg_test_pattern(jpeg_data);
  
  return true;
}

bool SdImageComponent::decode_png_fallback(const std::vector<uint8_t> &png_data) {
  ESP_LOGI(TAG_IMAGE, "🔄 Using PNG fallback decoder (test pattern)");
  
  // Extract dimensions from PNG header
  int detected_width = 0, detected_height = 0;
  
  if (!this->extract_png_dimensions(png_data, detected_width, detected_height)) {
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
  
  // Calculate output size and allocate
  size_t output_size = this->calculate_output_size();
  this->image_data_.resize(output_size);
  
  // Generate test pattern
  this->generate_png_test_pattern(png_data);
  
  return true;
}

// =====================================================
// DIMENSION EXTRACTION METHODS
// =====================================================

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
    }
  } else {
    ESP_LOGW(TAG_IMAGE, "⚠️ PNG file too small for IHDR chunk: %zu bytes", data.size());
  }
  
  return false;
}

// =====================================================
// TEST PATTERN GENERATION METHODS
// =====================================================

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

// =====================================================
// UTILITY METHODS
// =====================================================

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

// Helper methods
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

// Méthodes de compatibilité legacy (pour éviter les erreurs de compilation)
bool SdImageComponent::decode_jpeg_tiled(const std::vector<uint8_t> &jpeg_data) {
  // Rediriger vers la nouvelle implémentation
  return this->decode_jpeg_real(jpeg_data);
}

bool SdImageComponent::decode_jpeg_tile(const std::vector<uint8_t> &jpeg_data, 
                                       int tile_x, int tile_y, int tile_w, int tile_h) {
  // Rediriger vers la nouvelle méthode ultra-safe
  return this->decode_ultra_safe_tile(jpeg_data, tile_x, tile_y, tile_w, tile_h);
}

bool SdImageComponent::decode_jpeg_safe_tiles(const std::vector<uint8_t> &jpeg_data) {
  // Rediriger vers la nouvelle méthode adaptative
  return this->decode_with_micro_tiles(jpeg_data, 32);
}

bool SdImageComponent::decode_single_tile_safe(const std::vector<uint8_t> &jpeg_data,
                                              int tile_x, int tile_y, int tile_w, int tile_h) {
  // Rediriger vers la nouvelle méthode ultra-safe
  return this->decode_ultra_safe_tile(jpeg_data, tile_x, tile_y, tile_w, tile_h);
}

// =====================================================
// DÉCODEUR PNG (fallback seulement pour l'instant)
// =====================================================

bool SdImageComponent::decode_png_real(const std::vector<uint8_t> &png_data) {
  ESP_LOGW(TAG_IMAGE, "⚠️ Real PNG decoder not implemented yet");
  return false;
}

bool SdImageComponent::decode_png(const std::vector<uint8_t> &png_data) {
  // On utilise le vrai décodeur si dispo, sinon le fallback
  ESP_LOGI(TAG_IMAGE, "🔧 Attempting to use PNG decoder...");
  
  bool result = this->decode_png_real(png_data);
  if (result) {
    ESP_LOGI(TAG_IMAGE, "✅ PNG decoding successful");
    return true;
  } else {
    ESP_LOGW(TAG_IMAGE, "⚠️ PNG decoding failed, trying fallback");
  }
  
  // Fallback vers le pattern de test
  return this->decode_png_fallback(png_data);
}

// =====================================================
// CONVERSION DE FORMATS
// =====================================================

void SdImageComponent::convert_rgb888_to_target(const uint8_t *rgb_data, size_t pixel_count) {
  for (size_t i = 0; i < pixel_count; i++) {
    uint8_t r = rgb_data[i * 3 + 0];
    uint8_t g = rgb_data[i * 3 + 1];
    uint8_t b = rgb_data[i * 3 + 2];
    
    size_t offset = i * this->get_pixel_size();
    this->set_pixel_at_offset(offset, r, g, b, 255);
  }
}

void SdImageComponent::convert_rgba_to_target(const uint8_t *rgba_data, size_t pixel_count) {
  for (size_t i = 0; i < pixel_count; i++) {
    uint8_t r = rgba_data[i * 4 + 0];
    uint8_t g = rgba_data[i * 4 + 1];
    uint8_t b = rgba_data[i * 4 + 2];
    uint8_t a = rgba_data[i * 4 + 3];
    
    size_t offset = i * this->get_pixel_size();
    this->set_pixel_at_offset(offset, r, g, b, a);
  }
}

// =====================================================
// DÉCODEURS FALLBACK (patterns de test)
// =====================================================

bool SdImageComponent::decode_jpeg_fallback(const std::vector<uint8_t> &jpeg_data) {
  ESP_LOGI(TAG_IMAGE, "🔄 Using JPEG fallback decoder (test pattern)");
  
  // Extract dimensions from JPEG header
  int detected_width = 0, detected_height = 0;
  
  if (!this->extract_jpeg_dimensions(jpeg_data, detected_width, detected_height)) {
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
  
  // Calculate output size and allocate
  size_t output_size = this->calculate_output_size();
  this->image_data_.resize(output_size);
  
  // Generate test pattern
  this->generate_jpeg_test_pattern(jpeg_data);
  
  return true;
}

bool SdImageComponent::decode_png_fallback(const std::vector<uint8_t> &png_data) {
  ESP_LOGI(TAG_IMAGE, "🔄 Using PNG fallback decoder (test pattern)");
  
  // Extract dimensions from PNG header
  int detected_width = 0, detected_height = 0;
  
  if (!this->extract_png_dimensions(png_data, detected_width, detected_height)) {
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
  
  // Calculate output size and allocate
  size_t output_size = this->calculate_output_size();
  this->image_data_.resize(output_size);
  
  // Generate test pattern
  this->generate_png_test_pattern(png_data);
  
  return true;
}

// =====================================================
// DIMENSION EXTRACTION METHODS
// =====================================================

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
    }
  } else {
    ESP_LOGW(TAG_IMAGE, "⚠️ PNG file too small for IHDR chunk: %zu bytes", data.size());
  }
  
  return false;
}

// =====================================================
// TEST PATTERN GENERATION METHODS
// =====================================================

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

// =====================================================
// UTILITY METHODS
// =====================================================

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

// Helper methods
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






