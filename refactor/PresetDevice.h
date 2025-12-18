#pragma once
#include <string>
#include <atomic>
#include "PresetLinkageSharedMemory.h"

// 设备类型
enum class DeviceType { Left, Right };

// 设备逻辑封装类
class PresetDevice {
public:
    PresetDevice(DeviceType type, PresetLinkageSharedMemory& shm);

    int PresetMin() const;
    int PresetMax() const;
    bool ParsePreset(const std::string& input, int& preset) const;
    void UpdatePreset(int preset);

    // 监控对方设备信息的线程
    void ObserverThread(std::atomic<bool>& running);

private:
    DeviceType deviceType_;
    PresetLinkageSharedMemory& shm_;
};