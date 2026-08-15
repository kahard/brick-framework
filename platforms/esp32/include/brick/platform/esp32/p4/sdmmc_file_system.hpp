#pragma once

#include <cstdio>
#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/file_system.hpp"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32 {

class SdmmcFile final : public interfaces::storage::IFile {
 public:
  explicit SdmmcFile(std::FILE* handle) : handle_(handle) {}
  ~SdmmcFile() override {
    if (handle_ != nullptr) std::fclose(handle_);
  }

  std::size_t read(void* buffer, std::size_t size, std::size_t count) override {
    return handle_ == nullptr ? 0 : std::fread(buffer, size, count, handle_);
  }

  bool seek(long offset, int origin) override {
    return handle_ != nullptr && std::fseek(handle_, offset, origin) == 0;
  }

 private:
  std::FILE* handle_;
};

class SdmmcFileSystem final : public interfaces::storage::IFileSystem {
 public:
  bool mount() override {
    if (mounted_) return true;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 10000;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_43;
    slot.cmd = GPIO_NUM_44;
    slot.d0 = GPIO_NUM_39;
    slot.d1 = GPIO_NUM_40;
    slot.d2 = GPIO_NUM_41;
    slot.d3 = GPIO_NUM_42;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    const esp_err_t result = esp_vfs_fat_sdmmc_mount(
        "/sdcard", &host, &slot, &config, &card_);
    if (result != ESP_OK) {
      ESP_LOGE(TAG, "SDMMC mount failed: %s", esp_err_to_name(result));
      return false;
    }

    mounted_ = true;
    ESP_LOGI(TAG, "SDMMC mounted at /sdcard");
    return true;
  }

  std::vector<std::string> list_files(const char* path) override {
    std::vector<std::string> files;
    DIR* directory = opendir(path);
    if (directory == nullptr) return files;
    struct dirent* entry;
    while ((entry = readdir(directory)) != nullptr) {
      if (entry->d_type == DT_REG) {
        files.emplace_back(std::string(path) + "/" + entry->d_name);
      }
    }
    closedir(directory);
    return files;
  }

  std::unique_ptr<interfaces::storage::IFile> open(
      const char* path, const char* mode) override {
    std::FILE* handle = std::fopen(path, mode);
    if (handle == nullptr) return nullptr;
    return std::make_unique<SdmmcFile>(handle);
  }

 private:
  static constexpr const char* TAG = "brick_sdmmc";
  bool mounted_ = false;
  sdmmc_card_t* card_ = nullptr;
};

}  // namespace brick::platform::esp32
