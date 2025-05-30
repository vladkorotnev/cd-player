#pragma once
#include <AudioTools.h>
#include <FTPClient.h>

class FTPSource : public AudioSource {
 public:
  FTPSource(FTPClient<WiFiClient>& client, const std::string pfx, const std::vector<std::string> names) {
    files = names;
    prefix = pfx;
    p_client = &client;
    timeout_auto_next_value = 5000;
  }

  /// Resets the actual data
  void begin()override {
    TRACED();
    idx = 0;
  }

  /// Resets the actual data
  void end() {
    idx = 0;
  }

  /// Returns next audio stream
  Stream* nextStream(int offset) override {
    int tmp_idx = idx + offset;
    if (!isValidIdx(tmp_idx)) return nullptr;
    return selectStream(tmp_idx);
  };

  /// Returns previous audio stream
  Stream* previousStream(int offset) override { return nextStream(-offset); };

  /// Returns audio stream at the indicated index (the index is zero based, so
  /// the first value is 0!)
  Stream* selectStream(int index) override {
    if (!isValidIdx(index)) return nullptr;
    idx = index;
    if (file) {
       file.close();  // close the previous file
    }
    std::string path = prefix + files[idx];
    ESP_LOGI(LOG_TAG, "Open: [%s]", path.c_str());
    file = p_client->open(path.c_str());
    return &file;
  }

  /// Returns the number of available files
  size_t size() { return files.size(); }

  Stream* selectStream(const char* path) override { return nullptr; }

  std::string currentFileName() {
    return files[idx];
  }

 protected:
  std::vector<std::string> files;
  std::string prefix;
  FTPClient<WiFiClient>* p_client = nullptr;
  FTPFile file;
  int idx = 0;
  const char* p_ext = nullptr;
  const char* p_path = "/";

  bool isValidIdx(int index) {
    if (index < 0) return false;
    if (index >= files.size()) {
      ESP_LOGE(LOG_TAG, "index %d is out of range (size: %d)", index, files.size());
      return false;
    }
    return true;
  }

  const char * LOG_TAG = "FTPSource";
};