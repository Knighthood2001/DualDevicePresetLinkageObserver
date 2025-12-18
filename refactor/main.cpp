#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <Windows.h>
#include "PresetDevice.h"
#include "PresetLinkageSharedMemory.h"

// 选择设备类型
DeviceType SelectDeviceType() {
    std::string type;
    while (true) {
        std::cout << "请选择当前设备类型（left/right）: ";
        std::cin >> type;
        for (auto& c : type) c = tolower(c);
        if (type == "left") return DeviceType::Left;
        if (type == "right") return DeviceType::Right;
        std::cout << "输入错误，仅支持left或right。" << std::endl;
    }
}

// 显示初始界面 (头行+第2行状态行)
void InitScreen(const std::string& deviceTips) {
    system("cls");
    std::cout << "===== DualDevicePresetLinkageObserver (多文件封装与状态区版) =====" << std::endl;
    // 输出一行空行(占据第2行)
    std::cout << std::endl;
    // 状态区先显示提示信息
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = { 0, 1 }; // 第2行
    SetConsoleCursorPosition(hStdout, coord);
    std::cout << "等待联动/对方输入..." << std::flush;
    // 恢复到输入区（第3行以后）
    coord = { 0, 3 };
    SetConsoleCursorPosition(hStdout, coord);
    std::cout << deviceTips << std::endl;
}

int main() {
    PresetLinkageSharedMemory shm;
    if (!shm.Init()) {
        std::cerr << "共享内存或互斥体创建失败！" << std::endl;
        return 1;
    }
    DeviceType deviceType = SelectDeviceType();
    std::cin.ignore();

    std::string deviceTips = "输入q退出。";
    InitScreen(deviceTips);

    PresetDevice device(deviceType, shm);

    std::atomic<bool> running(true);
    std::thread observer([&]() { device.ObserverThread(running); });

    std::string input_str;
    int preset;
    while (running) {
        // 输入区始终会在第4行往下（不会被联动区覆盖）
        std::cout << "请输入预置位（" << device.PresetMin() << "-" << device.PresetMax() << "，q退出）: ";
        std::getline(std::cin, input_str);
        if (!device.ParsePreset(input_str, preset)) { running = false; break; }
        if (!input_str.empty()) device.UpdatePreset(preset);
    }

    if (observer.joinable()) observer.join();
    shm.Release();
    // 光标移至最后
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = { 0, 10 };
    SetConsoleCursorPosition(hStdout, coord);

    std::cout << "\n程序已退出！" << std::endl;
    return 0;
}