#include "PresetLinkageSharedMemory.h"
#include <cstring>

PresetLinkageSharedMemory::PresetLinkageSharedMemory()
    : hMapFile_(NULL), pData_(nullptr), hMutex_(NULL) {}

PresetLinkageSharedMemory::~PresetLinkageSharedMemory() {
    Release();
}

bool PresetLinkageSharedMemory::Init() {
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

void PresetLinkageSharedMemory::Release() {
    if (hMutex_ && pData_) {
        WaitForSingleObject(hMutex_, INFINITE);
        pData_->exit_flag = true;
        ReleaseMutex(hMutex_);
    }
    if (pData_) UnmapViewOfFile(pData_);
    if (hMapFile_) CloseHandle(hMapFile_);
    if (hMutex_) CloseHandle(hMutex_);
}

PresetLinkageData* PresetLinkageSharedMemory::Data() { return pData_; }
HANDLE PresetLinkageSharedMemory::Mutex() { return hMutex_; }