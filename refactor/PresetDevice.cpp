#include "PresetDevice.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <Windows.h>

// 联动相关常量和映射
const int LEFT_PRESET_MIN = 1, LEFT_PRESET_MAX = 6;
const int RIGHT_PRESET_MIN = 7, RIGHT_PRESET_MAX = 12;
const int LEFT_TO_RIGHT_MAP[3] = { 9, 8, 7 }; // left:4->9,5->8,6->7
const int RIGHT_TO_LEFT_MAP[3] = { 6, 5, 4 }; // right:7->6,8->5,9->4

// 帮助函数：在控制台第2行输出状态并清空该行内容
void PrintStatusAtLine2(const std::string& status) {
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hStdout, &csbi);

    // 第2行，X=0
    COORD coord = { 0, 1 };
    SetConsoleCursorPosition(hStdout, coord);

    // 清行
    DWORD written;
    FillConsoleOutputCharacterA(hStdout, ' ', csbi.dwSize.X, coord, &written);
    SetConsoleCursorPosition(hStdout, coord);

    // 输出状态
    std::cout << status << std::flush;

    // 恢复光标到输入区（第5行，假定输入区不变，否则可以传参指定，或记住之前Y即可）
    // 这里不处理，输入区用std::getline本身会正常
}

// ----------- PresetDevice 成员实现 ------------

PresetDevice::PresetDevice(DeviceType type, PresetLinkageSharedMemory& shm)
    : deviceType_(type), shm_(shm) {}

int PresetDevice::PresetMin() const { return (deviceType_ == DeviceType::Left) ? LEFT_PRESET_MIN : RIGHT_PRESET_MIN; }
int PresetDevice::PresetMax() const { return (deviceType_ == DeviceType::Left) ? LEFT_PRESET_MAX : RIGHT_PRESET_MAX; }

bool PresetDevice::ParsePreset(const std::string& input, int& preset) const {
    if (input == "q" || input == "Q") return false;
    try { preset = std::stoi(input); }
    catch (...) { PrintStatusAtLine2("输入错误！请输入数字（或q退出）。"); return true; }
    if (preset < PresetMin() || preset > PresetMax()) {
        PrintStatusAtLine2("输入错误！预置位范围：" + std::to_string(PresetMin()) + "-" + std::to_string(PresetMax()) + "。");
        return true;
    }
    return true;
}

void PresetDevice::UpdatePreset(int preset) {
    WaitForSingleObject(shm_.Mutex(), INFINITE);
    PresetLinkageData* pData = shm_.Data();
    std::string msg;
    if (deviceType_ == DeviceType::Left) {
        strcpy_s(pData->left_preset, std::to_string(preset).c_str());
        pData->right_linkage_target = (preset >= 4 && preset <= 6) ? LEFT_TO_RIGHT_MAP[preset - 4] : 0;
        pData->left_updated = true;
        msg = "已设置左侧预置位为：" + std::to_string(preset);
        if (pData->right_linkage_target)
            msg += " → 联动右侧：" + std::to_string(pData->right_linkage_target);
    } else {
        strcpy_s(pData->right_preset, std::to_string(preset).c_str());
        pData->left_linkage_target = (preset >= 7 && preset <= 9) ? RIGHT_TO_LEFT_MAP[preset - 7] : 0;
        pData->right_updated = true;
        msg = "已设置右侧预置位为：" + std::to_string(preset);
        if (pData->left_linkage_target)
            msg += " → 联动左侧：" + std::to_string(pData->left_linkage_target);
    }
    ReleaseMutex(shm_.Mutex());
    PrintStatusAtLine2(msg);
}

void PresetDevice::ObserverThread(std::atomic<bool>& running) {
    const char* other_preset = nullptr;
    int linkage_target = 0;
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        WaitForSingleObject(shm_.Mutex(), INFINITE);
        if (shm_.Data()->exit_flag) { running = false; }
        if (deviceType_ == DeviceType::Left) {
            if (shm_.Data()->right_updated && shm_.Data()->right_preset[0]) {
                other_preset = shm_.Data()->right_preset;
                linkage_target = shm_.Data()->left_linkage_target;
                shm_.Data()->right_updated = false;
                std::string status = "[right]预置位：" + std::string(other_preset);
                if (linkage_target) status += " → 联动要求左切：" + std::to_string(linkage_target);
                PrintStatusAtLine2(status);
            }
        } else {
            if (shm_.Data()->left_updated && shm_.Data()->left_preset[0]) {
                other_preset = shm_.Data()->left_preset;
                linkage_target = shm_.Data()->right_linkage_target;
                shm_.Data()->left_updated = false;
                std::string status = "[left]预置位：" + std::string(other_preset);
                if (linkage_target) status += " → 联动要求右切：" + std::to_string(linkage_target);
                PrintStatusAtLine2(status);
            }
        }
        ReleaseMutex(shm_.Mutex());
    }
}