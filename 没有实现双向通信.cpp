#include <Windows.h>
#include <iostream>
#include <string>
#include <cctype>
#include <cstring>

// 共享内存/互斥体命名（保证双进程通信）
#define SHARED_MEM_NAME L"DDPresetObserver_SharedMem"
#define MUTEX_NAME      L"DDPresetObserver_Mutex"

// 共享内存数据结构（跨进程同步核心数据）
struct PresetLinkageData {
   char left_preset[16];    // 左侧设备当前预置位（字符串，兼容"q"退出指令）
   char right_preset[16];   // 右侧设备当前预置位（字符串，兼容"q"退出指令）
   int left_linkage_target; // 左侧需联动到的预置位（0=无联动）
   int right_linkage_target;// 右侧需联动到的预置位（0=无联动）
};

// 设备预置位范围
const int LEFT_PRESET_MIN = 1;
const int LEFT_PRESET_MAX = 6;
const int RIGHT_PRESET_MIN = 7;
const int RIGHT_PRESET_MAX = 12;

// 修正后的联动映射表（核心）
// left:4→9,5→8,6→7 | right:7→6,8→5,9→4
const int LEFT_TO_RIGHT_MAP[3] = { 9, 8, 7 };  // left:4→索引0→9, 5→索引1→8, 6→索引2→7
const int RIGHT_TO_LEFT_MAP[3] = { 6, 5, 4 };  // right:7→索引0→6,8→索引1→5,9→索引2→4

// 函数声明
std::string SelectDeviceType();
bool ParsePresetInput(const std::string& input, int& preset, const std::string& device_type);
int CalculateLeftLinkageTarget(int right_preset);
int CalculateRightLinkageTarget(int left_preset);
PresetLinkageData* OpenSharedMemory(HANDLE& hMapFile, bool& is_new);
void ReleaseSharedMemory(HANDLE hMapFile, PresetLinkageData* pSharedData);

// 选择设备类型（仅初始化执行一次）
std::string SelectDeviceType() {
   std::string device_type;
   while (true) {
       std::cout << "===== 设备类型选择 =====" << std::endl;
       std::cout << "请选择当前设备类型（输入 left / right）：";
       std::cin >> device_type;

       // 转小写
       for (char& c : device_type) c = tolower(c);

       if (device_type == "left" || device_type == "right") {
           std::cout << "已选定设备类型：" << device_type << "\n" << std::endl;
           break;
       }
       std::cout << "输入错误！仅支持 left / right，请重新输入。\n" << std::endl;
   }
   return device_type;
}

// 解析预置位输入（支持数字/退出指令q）
bool ParsePresetInput(const std::string& input, int& preset, const std::string& device_type) {
   // 退出指令
   if (input == "q" || input == "Q") {
       return false;
   }

   // 解析数字
   try {
       preset = std::stoi(input);
   }
   catch (...) {
       std::cout << "输入错误！请输入数字（或q退出）。\n" << std::endl;
       return true;
   }

   // 校验范围
   int min_p = (device_type == "left") ? LEFT_PRESET_MIN : RIGHT_PRESET_MIN;
   int max_p = (device_type == "left") ? LEFT_PRESET_MAX : RIGHT_PRESET_MAX;
   if (preset < min_p || preset > max_p) {
       std::cout << "输入错误！" << device_type << "设备预置位范围："
           << min_p << "-" << max_p << "（或q退出）。\n" << std::endl;
       return true;
   }

   return true;
}

// 计算左侧设备的联动目标（由右侧预置位推导）
int CalculateLeftLinkageTarget(int right_preset) {
   if (right_preset >= 7 && right_preset <= 9) {
       return RIGHT_TO_LEFT_MAP[right_preset - 7]; // 7→0→6, 8→1→5,9→2→4
   }
   return 0;
}

// 计算右侧设备的联动目标（由左侧预置位推导）
int CalculateRightLinkageTarget(int left_preset) {
   if (left_preset >= 4 && left_preset <= 6) {
       return LEFT_TO_RIGHT_MAP[left_preset - 4]; //4→0→9,5→1→8,6→2→7
   }
   return 0;
}

// 打开/创建共享内存
PresetLinkageData* OpenSharedMemory(HANDLE& hMapFile, bool& is_new) {
   is_new = false;
   // 尝试打开已有共享内存
   hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
   if (hMapFile == NULL) {
       // 创建新共享内存
       hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
           0, sizeof(PresetLinkageData), SHARED_MEM_NAME);
       if (hMapFile == NULL) {
           std::cerr << "创建共享内存失败，错误码：" << GetLastError() << std::endl;
           return nullptr;
       }
       is_new = true;
   }

   // 映射到进程地址空间
   PresetLinkageData* pData = (PresetLinkageData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PresetLinkageData));
   if (pData == NULL) {
       std::cerr << "映射共享内存失败，错误码：" << GetLastError() << std::endl;
       CloseHandle(hMapFile);
       return nullptr;
   }

   // 初始化新共享内存
   if (is_new) {
       ZeroMemory(pData, sizeof(PresetLinkageData));
       strcpy_s(pData->left_preset, sizeof(pData->left_preset), "");
       strcpy_s(pData->right_preset, sizeof(pData->right_preset), "");
       pData->left_linkage_target = 0;
       pData->right_linkage_target = 0;
   }

   return pData;
}

// 释放共享内存资源
void ReleaseSharedMemory(HANDLE hMapFile, PresetLinkageData* pSharedData) {
   if (pSharedData) UnmapViewOfFile(pSharedData);
   if (hMapFile) CloseHandle(hMapFile);
}

int main() {
   std::cout << "===== DualDevicePresetLinkageObserver (循环版) =====" << std::endl;
   std::cout << "提示：输入q/Q可退出程序\n" << std::endl;

   // 1. 创建互斥体（保证共享内存读写安全）
   HANDLE hMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
   if (hMutex == NULL) {
       std::cerr << "创建互斥体失败，错误码：" << GetLastError() << std::endl;
       return 1;
   }

   // 2. 打开共享内存
   HANDLE hMapFile = NULL;
   bool is_new_shmem = false;
   PresetLinkageData* pSharedData = OpenSharedMemory(hMapFile, is_new_shmem);
   if (pSharedData == NULL) {
       CloseHandle(hMutex);
       return 1;
   }

   // 3. 选择设备类型（仅一次）
   std::string device_type = SelectDeviceType();
   std::string input_str;
   int current_preset = 0;

   // 4. 循环执行预置位输入+联动计算
   while (true) {
       // 提示输入
       std::cout << "===== 预置位输入 =====" << std::endl;
       std::cout << "请输入" << device_type << "设备预置位（或q退出）：";
       std::cin >> input_str;

       // 解析输入（退出则终止循环）
       if (!ParsePresetInput(input_str, current_preset, device_type)) {
           std::cout << "收到退出指令，正在清理资源..." << std::endl;
           break;
       }

       // 加锁操作共享内存
       WaitForSingleObject(hMutex, INFINITE);

       // 更新当前设备预置位到共享内存
       if (device_type == "left") {
           strcpy_s(pSharedData->left_preset, sizeof(pSharedData->left_preset), std::to_string(current_preset).c_str());
           // 计算右侧联动目标并更新
           pSharedData->right_linkage_target = CalculateRightLinkageTarget(current_preset);
           // 读取右侧当前预置位（展示用）
           std::string right_current = (strcmp(pSharedData->right_preset, "") == 0) ? "未设置" : pSharedData->right_preset;
           std::cout << "\n【左侧设备状态】" << std::endl;
           std::cout << "当前预置位：" << current_preset << std::endl;
           std::cout << "右侧设备当前预置位：" << right_current << std::endl;
           if (pSharedData->right_linkage_target != 0) {
               std::cout << "联动要求：右侧设备需切换到预置位" << pSharedData->right_linkage_target << std::endl;
           }
           else {
               std::cout << "联动要求：无" << std::endl;
           }
       }
       else { // right设备
           strcpy_s(pSharedData->right_preset, sizeof(pSharedData->right_preset), std::to_string(current_preset).c_str());
           // 计算左侧联动目标并更新
           pSharedData->left_linkage_target = CalculateLeftLinkageTarget(current_preset);
           // 读取左侧当前预置位（展示用）
           std::string left_current = (strcmp(pSharedData->left_preset, "") == 0) ? "未设置" : pSharedData->left_preset;
           std::cout << "\n【右侧设备状态】" << std::endl;
           std::cout << "当前预置位：" << current_preset << std::endl;
           std::cout << "左侧设备当前预置位：" << left_current << std::endl;
           if (pSharedData->left_linkage_target != 0) {
               std::cout << "联动要求：左侧设备需切换到预置位" << pSharedData->left_linkage_target << std::endl;
           }
           else {
               std::cout << "联动要求：无" << std::endl;
           }
       }

       // 释放互斥体
       ReleaseMutex(hMutex);
       std::cout << "\n----------------------------------------\n" << std::endl;
   }

   // 5. 释放资源
   ReleaseSharedMemory(hMapFile, pSharedData);
   CloseHandle(hMutex);

   std::cout << "程序已退出！" << std::endl;
   return 0;
}