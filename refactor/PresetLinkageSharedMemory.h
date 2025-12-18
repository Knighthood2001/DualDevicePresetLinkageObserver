#pragma once
#include <Windows.h>

// 共享内存数据结构
struct PresetLinkageData {
    char left_preset[16];
    char right_preset[16];
    int left_linkage_target;
    int right_linkage_target;
    bool left_updated;
    bool right_updated;
    bool exit_flag;
};

// 封装共享内存及互斥体
class PresetLinkageSharedMemory {
public:
    PresetLinkageSharedMemory();
    ~PresetLinkageSharedMemory();

    bool Init();
    void Release();
    PresetLinkageData* Data();
    HANDLE Mutex();

private:
    HANDLE hMapFile_;
    PresetLinkageData* pData_;
    HANDLE hMutex_;
};