#include <Windows.h>
#include <iostream>
#include <string>
#include <cctype>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

// 设备枚举类型
enum class DeviceType { Left, Right };

// 预置位范围常量
const int LEFT_PRESET_MIN = 1, LEFT_PRESET_MAX = 6;
const int RIGHT_PRESET_MIN = 7, RIGHT_PRESET_MAX = 12;
const int LEFT_TO_RIGHT_MAP[3] = { 9, 8, 7 };
const int RIGHT_TO_LEFT_MAP[3] = { 6, 5, 4 };

// 共享内存结构
struct PresetLinkageData {
    char left_preset[16];
    char right_preset[16];
    int left_linkage_target;
    int right_linkage_target;
    bool left_updated;
    bool right_updated;
    bool exit_flag;
};

// 共享内存（带互斥体）的管理类
class PresetLinkageSharedMemory {
public:
    PresetLinkageSharedMemory() : hMapFile_(NULL), pData_(nullptr), hMutex_(NULL) {}
    ~PresetLinkageSharedMemory() { Release(); }

    bool Init() {
        hMutex_ = CreateMutex(NULL, FALSE, L"DDPresetObserver_Mutex");
        if (!hMutex_) return false;
        hMapFile_ = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, L"DDPresetObserver_SharedMem");
        bool is_new = false;
        if (!hMapFile_) {
            hMapFile_ = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(PresetLinkageData), L"DDPresetObserver_SharedMem");
            is_new = true;
        }
        if (!hMapFile_) return false;
        pData_ = (PresetLinkageData*)MapViewOfFile(hMapFile_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PresetLinkageData));
        if (!pData_) return false;
        if (is_new) ZeroMemory(pData_, sizeof(PresetLinkageData));
        return true;
    }

    void Release() {
        if (hMutex_ && pData_) {
            WaitForSingleObject(hMutex_, INFINITE);
            pData_->exit_flag = true;
            ReleaseMutex(hMutex_);
        }
        if (pData_) UnmapViewOfFile(pData_);
        if (hMapFile_) CloseHandle(hMapFile_);
        if (hMutex_) CloseHandle(hMutex_);
    }

    PresetLinkageData* Data() { return pData_; }
    HANDLE Mutex() { return hMutex_; }

private:
    HANDLE hMapFile_;
    PresetLinkageData* pData_;
    HANDLE hMutex_;
};

// 单个设备业务逻辑封装
class PresetDevice {
public:
    PresetDevice(DeviceType type, PresetLinkageSharedMemory& shm)
        : deviceType_(type), shm_(shm) {}

    int PresetMin() const { return (deviceType_ == DeviceType::Left) ? LEFT_PRESET_MIN : RIGHT_PRESET_MIN; }
    int PresetMax() const { return (deviceType_ == DeviceType::Left) ? LEFT_PRESET_MAX : RIGHT_PRESET_MAX; }

    // 解析输入
    bool ParsePreset(const std::string& input, int& preset) const {
        if (input == "q" || input == "Q") return false;
        try { preset = std::stoi(input); }
        catch (...) { std::cout << "输入错误！请输入数字（或q退出）。\n"; return true; }
        if (preset < PresetMin() || preset > PresetMax()) {
            std::cout << "输入错误！预置位范围：" << PresetMin() << "-" << PresetMax() << "。\n"; return true;
        }
        return true;
    }

    // 更新共享内存及联动输出
    void UpdatePreset(int preset) {
        WaitForSingleObject(shm_.Mutex(), INFINITE);
        PresetLinkageData* pData = shm_.Data();
        if (deviceType_ == DeviceType::Left) {
            strcpy_s(pData->left_preset, std::to_string(preset).c_str());
            pData->right_linkage_target = (preset >= 4 && preset <= 6) ? LEFT_TO_RIGHT_MAP[preset - 4] : 0;
            pData->left_updated = true;
            std::cout << "已设置左侧预置位为：" << preset;
            if (pData->right_linkage_target) std::cout << " → 联动右侧：" << pData->right_linkage_target;
        }
        else {
            strcpy_s(pData->right_preset, std::to_string(preset).c_str());
            pData->left_linkage_target = (preset >= 7 && preset <= 9) ? RIGHT_TO_LEFT_MAP[preset - 7] : 0;
            pData->right_updated = true;
            std::cout << "已设置右侧预置位为：" << preset;
            if (pData->left_linkage_target) std::cout << " → 联动左侧：" << pData->left_linkage_target;
        }
        std::cout << std::endl;
        ReleaseMutex(shm_.Mutex());
    }

    // 监控对方设备信息的线程
    void ObserverThread(std::atomic<bool>& running) {
        const char* other_preset = nullptr;
        int linkage_target = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            WaitForSingleObject(shm_.Mutex(), INFINITE);
            if (shm_.Data()->exit_flag) { running = false; }
            // 这里只显示状态，省略光标控制
            if (deviceType_ == DeviceType::Left) {
                if (shm_.Data()->right_updated && shm_.Data()->right_preset[0]) {
                    other_preset = shm_.Data()->right_preset;
                    linkage_target = shm_.Data()->left_linkage_target;
                    shm_.Data()->right_updated = false;
                    std::cout << "[right]预置位：" << other_preset;
                    if (linkage_target) std::cout << " → 联动要求左切：" << linkage_target;
                    std::cout << std::endl;
                }
            }
            else {
                if (shm_.Data()->left_updated && shm_.Data()->left_preset[0]) {
                    other_preset = shm_.Data()->left_preset;
                    linkage_target = shm_.Data()->right_linkage_target;
                    shm_.Data()->left_updated = false;
                    std::cout << "[left]预置位：" << other_preset;
                    if (linkage_target) std::cout << " → 联动要求右切：" << linkage_target;
                    std::cout << std::endl;
                }
            }
            ReleaseMutex(shm_.Mutex());
        }
    }

private:
    DeviceType deviceType_;
    PresetLinkageSharedMemory& shm_;
};

// 辅助函数：选择设备类型
DeviceType SelectDeviceType() {
    std::string type;
    while (true) {
        std::cout << "请选择当前设备类型（left/right）：";
        std::cin >> type;
        for (auto& c : type) c = tolower(c);
        if (type == "left") return DeviceType::Left;
        if (type == "right") return DeviceType::Right;
        std::cout << "输入错误，仅支持left或right。" << std::endl;
    }
}

// 主函数
int main() {
    std::cout << "===== DualDevicePresetLinkageObserver (封装优化版) =====" << std::endl;
    PresetLinkageSharedMemory shm;
    if (!shm.Init()) { std::cerr << "共享内存或互斥体创建失败！" << std::endl; return 1; }
    DeviceType deviceType = SelectDeviceType();
    std::cin.ignore(); // 丢弃残余换行

    PresetDevice device(deviceType, shm);

    std::atomic<bool> running(true);
    std::thread observer([&]() { device.ObserverThread(running); });

    std::string input_str;
    int preset;
    while (running) {
        std::cout << "请输入预置位（" << device.PresetMin() << "-" << device.PresetMax() << "，q退出）: ";
        std::getline(std::cin, input_str);
        if (!device.ParsePreset(input_str, preset)) { running = false; break; }
        if (!input_str.empty()) device.UpdatePreset(preset);
    }

    if (observer.joinable()) observer.join();
    shm.Release();
    std::cout << "程序已退出！" << std::endl;
    return 0;
}