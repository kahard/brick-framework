#pragma once

#include <cstdio>
#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/file_system.hpp"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32 {

class File final : public interfaces::storage::IFile {
 public:
  explicit File(std::FILE* handle) : handle_(handle) {}
  ~File() override {
    if (handle_ != nullptr) {
      std::fclose(handle_);
    }
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

class FileSystem final : public interfaces::storage::IFileSystem {
 public:
  bool mount() override {
    if (mounted_) {
      return true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = SPI2_HOST;
    slot_cfg.gpio_cs = GPIO_NUM_42;
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    const esp_err_t result = esp_vfs_fat_sdspi_mount(
        "/sdcard", &host, &slot_cfg, &mount_cfg, &card_);
    if (result != ESP_OK) {
      ESP_LOGE(TAG, "SD mount: %s", esp_err_to_name(result));
      return false;
    }

    mounted_ = true;
    ESP_LOGI(TAG, "SD mounted at /sdcard");
    return true;
  }

  std::vector<std::string> list_files(const char* path) override {
    std::vector<std::string> files;
    DIR* directory = opendir(path);
    if (directory == nullptr) {
      return files;
    }

    struct dirent* entry;
    while ((entry = readdir(directory)) != nullptr) {
      if (entry->d_type != DT_REG) {
        continue;
      }
      files.emplace_back(std::string(path) + "/" + entry->d_name);
    }
    closedir(directory);
    return files;
  }

  std::unique_ptr<interfaces::storage::IFile> open(
      const char* path, const char* mode) override {
    std::FILE* handle = std::fopen(path, mode);
    if (handle == nullptr) {
      return nullptr;
    }
    return std::unique_ptr<interfaces::storage::IFile>(new File(handle));
  }

 private:
  static constexpr const char* TAG = "brick_fs";
  bool mounted_ = false;
  sdmmc_card_t* card_ = nullptr;
};

}  // namespace brick::platform::esp32
