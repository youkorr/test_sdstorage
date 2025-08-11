#include "sd_mmc.h"

#include <algorithm>

#include "math.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sd_mmc {

static const char *TAG = "sd_mmc";

#ifdef USE_SENSOR
FileSizeSensor::FileSizeSensor(sensor::Sensor *sensor, std::string const &path) : sensor(sensor), path(path) {}
#endif

void SdMmc::loop() {}

void SdMmc::dump_config() {
  ESP_LOGCONFIG(TAG, "SD MMC Component");
  ESP_LOGCONFIG(TAG, "  Mode 1 bit: %s", TRUEFALSE(this->mode_1bit_));
  ESP_LOGCONFIG(TAG, "  Slot: %d", this->slot_); 
  ESP_LOGCONFIG(TAG, "  CLK Pin: %d", this->clk_pin_);
  ESP_LOGCONFIG(TAG, "  CMD Pin: %d", this->cmd_pin_);
  ESP_LOGCONFIG(TAG, "  DATA0 Pin: %d", this->data0_pin_);
  if (!this->mode_1bit_) {
    ESP_LOGCONFIG(TAG, "  DATA1 Pin: %d", this->data1_pin_);
    ESP_LOGCONFIG(TAG, "  DATA2 Pin: %d", this->data2_pin_);
    ESP_LOGCONFIG(TAG, "  DATA3 Pin: %d", this->data3_pin_);
  }

  if (this->power_ctrl_pin_ != nullptr) {
    LOG_PIN("  Power Ctrl Pin: ", this->power_ctrl_pin_);
  }

#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Used space", this->used_space_sensor_);
  LOG_SENSOR("  ", "Total space", this->total_space_sensor_);
  LOG_SENSOR("  ", "Free space", this->free_space_sensor_);
  for (auto &sensor : this->file_size_sensors_) {
    if (sensor.sensor != nullptr)
      LOG_SENSOR("  ", "File size", sensor.sensor);
  }
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "SD Card Type", this->sd_card_type_text_sensor_);
#endif

  if (this->is_failed()) {
    ESP_LOGE(TAG, "Setup failed : %s", SdMmc::error_code_to_string(this->init_error_).c_str());
    return;
  }
}

void SdMmc::write_file(const char *path, const uint8_t *buffer, size_t len) {
  ESP_LOGV(TAG, "Writing to file: %s", path);
  this->write_file(path, buffer, len, "w");
}

void SdMmc::append_file(const char *path, const uint8_t *buffer, size_t len) {
  ESP_LOGV(TAG, "Appending to file: %s", path);
  this->write_file(path, buffer, len, "a");
}

std::vector<std::string> SdMmc::list_directory(const char *path, uint8_t depth) const {
  std::vector<std::string> list;
  std::vector<storage::FileInfo> infos = list_directory_file_info(path, depth);
  std::transform(infos.cbegin(), infos.cend(), list.begin(), [](storage::FileInfo const &info) { return info.path; });
  return list;
}

std::vector<std::string> SdMmc::list_directory(const std::string &path, uint8_t depth) const {
  return this->list_directory(path.c_str(), depth);
}

std::vector<storage::FileInfo> SdMmc::list_directory_file_info(const char *path, uint8_t depth) const {
  std::vector<storage::FileInfo> list;
  list_directory_file_info_rec(path, depth, list);
  return list;
}

std::vector<storage::FileInfo> SdMmc::list_directory_file_info(const std::string &path, uint8_t depth) const {
  return this->list_directory_file_info(path.c_str(), depth);
}

storage::FileInfo SdMmc::file_info(const std::string &path) const {
  return storage::FileInfo(path, this->file_size(path), this->is_directory(path));
}

size_t SdMmc::file_size(std::string const &path) const { return this->file_size(path.c_str()); }

bool SdMmc::is_directory(std::string const &path) const { return this->is_directory(path.c_str()); }

bool SdMmc::delete_file(std::string const &path) { return this->delete_file(path.c_str()); }

std::vector<uint8_t> SdMmc::read_file(std::string const &path) { return this->read_file(path.c_str()); }

size_t SdMmc::read_file_chunk(const std::string &path, size_t offset, uint8_t *buffer, size_t length) {
  return this->read_file_chunk(path.c_str(), offset, buffer, length);
}

#ifdef USE_SENSOR
void SdMmc::add_file_size_sensor(sensor::Sensor *sensor, std::string const &path) {
  this->file_size_sensors_.emplace_back(sensor, path);
}
#endif

void SdMmc::set_clk_pin(uint8_t pin) { this->clk_pin_ = pin; }

void SdMmc::set_cmd_pin(uint8_t pin) { this->cmd_pin_ = pin; }

void SdMmc::set_data0_pin(uint8_t pin) { this->data0_pin_ = pin; }

void SdMmc::set_data1_pin(uint8_t pin) { this->data1_pin_ = pin; }

void SdMmc::set_data2_pin(uint8_t pin) { this->data2_pin_ = pin; }

void SdMmc::set_data3_pin(uint8_t pin) { this->data3_pin_ = pin; }

void SdMmc::set_mode_1bit(bool b) { this->mode_1bit_ = b; }

void SdMmc::set_power_ctrl_pin(GPIOPin *pin) { this->power_ctrl_pin_ = pin; }
void SdMmc::set_mount_point(std::string mount_point) { this->mount_point_ = mount_point; }
// void SdMmc::set_slot(uint8_t slot) { this->slot_ = slot; }
void SdMmc::set_high_speed(bool high_speed) { this->high_speed_ = high_speed; }

std::string SdMmc::error_code_to_string(SdMmc::ErrorCode code) {
  switch (code) {
    case ErrorCode::ERR_PIN_SETUP:
      return "Failed to set pins";
    case ErrorCode::ERR_MOUNT:
      return "Failed to mount card";
    case ErrorCode::ERR_NO_CARD:
      return "No card found";
    default:
      return "Unknown error";
  }
}

long double convertBytes(uint64_t value, MemoryUnits unit) {
  return value * 1.0 / pow(1024, static_cast<uint64_t>(unit));
}

std::string memory_unit_to_string(MemoryUnits unit) {
  switch (unit) {
    case MemoryUnits::Byte:
      return "B";
    case MemoryUnits::KiloByte:
      return "KB";
    case MemoryUnits::MegaByte:
      return "MB";
    case MemoryUnits::GigaByte:
      return "GB";
    case MemoryUnits::TeraByte:
      return "TB";
    case MemoryUnits::PetaByte:
      return "PB";
  }
  return "unknown";
}

MemoryUnits memory_unit_from_size(size_t size) {
  short unit = MemoryUnits::Byte;
  double s = static_cast<double>(size);
  while (s >= 1024 && unit < MemoryUnits::PetaByte) {
    s /= 1024;
    unit++;
  }
  return static_cast<MemoryUnits>(unit);
}

std::string format_size(size_t size) {
  MemoryUnits unit = memory_unit_from_size(size);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.2f %s", convertBytes(size, unit), memory_unit_to_string(unit).c_str());
  return std::string(buffer);
}

}  // namespace sd_mmc
}  // namespace esphome
