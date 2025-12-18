#include <Windows.h>
#include <iostream>
#include <string>
#include <cctype>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

// 共享内存/互斥体命名
#define SHARED_MEM_NAME L"DDPresetObserver_SharedMem"
#define MUTEX_NAME      L"DDPresetObserver_Mutex"

// 共享内存数据结构（添加通知标志）
struct PresetLinkageData {
   char left_preset[16];    // 左侧设备当前预置位
   char right_preset[16];   // 右侧设备当前预置位
   int left_linkage_target; // 左侧需联动到的预置位
   int right_linkage_target;// 右侧需联动到的预置位
   bool left_updated;       // 左侧更新标志
   bool right_updated;      // 右侧更新标志
   bool exit_flag;          // 退出标志
};

// 设备预置位范围
const int LEFT_PRESET_MIN = 1;
const int LEFT_PRESET_MAX = 6;
const int RIGHT_PRESET_MIN = 7;
const int RIGHT_PRESET_MAX = 12;

// 联动映射表
const int LEFT_TO_RIGHT_MAP[3] = { 9, 8, 7 };  // left:4→9,5→8,6→7
const int RIGHT_TO_LEFT_MAP[3] = { 6, 5, 4 };  // right:7→6,8→5,9→4

// 全局变量
std::atomic<bool> g_exit_program(false);
HANDLE g_hMutex = NULL;
PresetLinkageData* g_pSharedData = NULL;

// 函数声明
std::string SelectDeviceType();
bool ParsePresetInput(const std::string& input, int& preset, const std::string& device_type);
int CalculateLeftLinkageTarget(int right_preset);
int CalculateRightLinkageTarget(int left_preset);
PresetLinkageData* OpenSharedMemory(HANDLE& hMapFile, bool& is_new);
void ReleaseSharedMemory(HANDLE hMapFile, PresetLinkageData* pSharedData);
void ObserverThread(const std::string& device_type);
void ClearConsoleLine();

// 选择设备类型
std::string SelectDeviceType() {
   std::string device_type;
   while (true) {
       std::cout << "===== 设备类型选择 =====" << std::endl;
       std::cout << "请选择当前设备类型（输入 left / right）：";
       std::cin >> device_type;

       for (char& c : device_type) c = tolower(c);

       if (device_type == "left" || device_type == "right") {
           std::cout << "已选定设备类型：" << device_type << "\n" << std::endl;
           break;
       }
       std::cout << "输入错误！仅支持 left / right，请重新输入。\n" << std::endl;
   }
   return device_type;
}

// 解析预置位输入
bool ParsePresetInput(const std::string& input, int& preset, const std::string& device_type) {
   if (input == "q" || input == "Q") {
       return false;
   }

   try {
       preset = std::stoi(input);
   }
   catch (...) {
       std::cout << "输入错误！请输入数字（或q退出）。\n" << std::endl;
       return true;
   }

   int min_p = (device_type == "left") ? LEFT_PRESET_MIN : RIGHT_PRESET_MIN;
   int max_p = (device_type == "left") ? LEFT_PRESET_MAX : RIGHT_PRESET_MAX;
   if (preset < min_p || preset > max_p) {
       std::cout << "输入错误！" << device_type << "设备预置位范围："
           << min_p << "-" << max_p << "（或q退出）。\n" << std::endl;
       return true;
   }

   return true;
}

// 计算联动目标
int CalculateLeftLinkageTarget(int right_preset) {
   if (right_preset >= 7 && right_preset <= 9) {
       return RIGHT_TO_LEFT_MAP[right_preset - 7];
   }
   return 0;
}

int CalculateRightLinkageTarget(int left_preset) {
   if (left_preset >= 4 && left_preset <= 6) {
       return LEFT_TO_RIGHT_MAP[left_preset - 4];
   }
   return 0;
}

// 打开共享内存
PresetLinkageData* OpenSharedMemory(HANDLE& hMapFile, bool& is_new) {
   is_new = false;
   hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
   if (hMapFile == NULL) {
       hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
           0, sizeof(PresetLinkageData), SHARED_MEM_NAME);
       if (hMapFile == NULL) {
           std::cerr << "创建共享内存失败，错误码：" << GetLastError() << std::endl;
           return nullptr;
       }
       is_new = true;
   }

   PresetLinkageData* pData = (PresetLinkageData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PresetLinkageData));
   if (pData == NULL) {
       std::cerr << "映射共享内存失败，错误码：" << GetLastError() << std::endl;
       CloseHandle(hMapFile);
       return nullptr;
   }

   if (is_new) {
       ZeroMemory(pData, sizeof(PresetLinkageData));
       strcpy_s(pData->left_preset, sizeof(pData->left_preset), "");
       strcpy_s(pData->right_preset, sizeof(pData->right_preset), "");
       pData->left_linkage_target = 0;
       pData->right_linkage_target = 0;
       pData->left_updated = false;
       pData->right_updated = false;
       pData->exit_flag = false;
   }

   return pData;
}

// 释放共享内存
void ReleaseSharedMemory(HANDLE hMapFile, PresetLinkageData* pSharedData) {
   if (pSharedData) {
       // 设置退出标志通知对方
       if (g_hMutex && pSharedData) {
           WaitForSingleObject(g_hMutex, INFINITE);
           pSharedData->exit_flag = true;
           ReleaseMutex(g_hMutex);
       }
       UnmapViewOfFile(pSharedData);
   }
   if (hMapFile) CloseHandle(hMapFile);
}

// 清空控制台当前行
void ClearConsoleLine() {
   CONSOLE_SCREEN_BUFFER_INFO csbi;
   HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
   if (GetConsoleScreenBufferInfo(hStdOut, &csbi)) {
       COORD cursorPosition = csbi.dwCursorPosition;
       cursorPosition.X = 0;
       SetConsoleCursorPosition(hStdOut, cursorPosition);
       DWORD written;
       FillConsoleOutputCharacter(hStdOut, ' ', csbi.dwSize.X, cursorPosition, &written);
       SetConsoleCursorPosition(hStdOut, cursorPosition);
   }
}

// 观察者线程（实时监控对方状态）
void ObserverThread(const std::string& device_type) {
   // 1. 确定“对方设备”类型（当前是left则对方是right，反之亦然
   const std::string other_device = (device_type == "left") ? "right" : "left";
   // 2. 线程主循环：只要全局退出标志为false，就持续监控
   while (!g_exit_program) {
       std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 100ms刷新一次
       // 2.2 加锁互斥体：保证共享内存读写原子性，防止多进程竞争
       WaitForSingleObject(g_hMutex, INFINITE);

       // 3. 跨进程退出逻辑：检测共享内存中的退出标志
       if (g_pSharedData->exit_flag) {
           ReleaseMutex(g_hMutex);  // 先解锁
           g_exit_program = true;   // 设置全局退出标志，让循环终止
           break;
       }
       // 4. 读取共享内存中“对方设备”的状态（根据当前设备类型分支）
       // 4.1 对方设备的“更新标志”（是否有新的预置位输入）
       bool updated = (device_type == "left") ? g_pSharedData->right_updated : g_pSharedData->left_updated;
       // 4.2 对方设备的当前预置位字符串
       char* other_preset = (device_type == "left") ? g_pSharedData->right_preset : g_pSharedData->left_preset;
       // 4.3 当前设备需要联动的目标位（由对方预置位推导的结果）
       int linkage_target = (device_type == "left") ? g_pSharedData->left_linkage_target : g_pSharedData->right_linkage_target;
       // 5. 核心逻辑：如果对方设备有更新，且预置位非空 → 刷新控制台显示
       if (updated && other_preset[0] != '\0') {
           // 清除之前的提示行
           HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
           CONSOLE_SCREEN_BUFFER_INFO csbi;
           GetConsoleScreenBufferInfo(hStdOut, &csbi);

           // 保存当前光标位置
           COORD savedPos = csbi.dwCursorPosition;

           // 移动到固定行显示状态（例如第2行）
           COORD statusPos = { 0, 2 };
           SetConsoleCursorPosition(hStdOut, statusPos);

           // 清空状态行
           DWORD written;
           FillConsoleOutputCharacter(hStdOut, ' ', csbi.dwSize.X, statusPos, &written);
           SetConsoleCursorPosition(hStdOut, statusPos);

           // 显示对方状态
           std::cout << "【" << other_device << "设备】预置位：" << other_preset;
           if (linkage_target != 0) {
               std::cout << "  → 联动要求：请切换到预置位 " << linkage_target;
           }

           // 清除更新标志
           if (device_type == "left") {
               g_pSharedData->right_updated = false;
           }
           else {
               g_pSharedData->left_updated = false;
           }

           // 恢复光标位置
           SetConsoleCursorPosition(hStdOut, savedPos);
       }
       // 6. 解锁互斥体：让其他进程/线程能访问共享内存
       ReleaseMutex(g_hMutex);
   }
}

int main() {
   std::cout << "===== DualDevicePresetLinkageObserver (实时双向版) =====" << std::endl;
   std::cout << "提示：输入q/Q可退出程序\n" << std::endl;

   // 1. 创建互斥体
   g_hMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
   if (g_hMutex == NULL) {
       std::cerr << "创建互斥体失败，错误码：" << GetLastError() << std::endl;
       return 1;
   }

   // 2. 打开共享内存
   HANDLE hMapFile = NULL;
   bool is_new_shmem = false;
   g_pSharedData = OpenSharedMemory(hMapFile, is_new_shmem);
   if (g_pSharedData == NULL) {
       CloseHandle(g_hMutex);
       return 1;
   }

   // 3. 选择设备类型
   std::string device_type = SelectDeviceType();

   // 4. 启动观察者线程
   std::thread observer(ObserverThread, device_type);

   // 5. 清屏并显示初始界面
   system("cls");
   std::cout << "===== " << device_type << "设备控制台 =====" << std::endl;
   std::cout << "提示：输入q/Q可退出程序\n" << std::endl;
   std::cout << "【状态显示区】" << std::endl;
   std::cout << "等待输入..." << std::endl;
   std::cout << "\n【输入区】" << std::endl;

   // 6. 主输入循环
   std::string input_str;
   int current_preset = 0;

   while (!g_exit_program) {
       // 显示输入提示
       std::cout << "请输入预置位（"
           << ((device_type == "left") ? LEFT_PRESET_MIN : RIGHT_PRESET_MIN)
           << "-"
           << ((device_type == "left") ? LEFT_PRESET_MAX : RIGHT_PRESET_MAX)
           << "）: ";

       // 获取输入
       std::getline(std::cin, input_str);

       // 检查退出
       if (g_exit_program) break;

       // 处理空输入
       if (input_str.empty()) continue;

       // 解析输入
       if (!ParsePresetInput(input_str, current_preset, device_type)) {
           g_exit_program = true;
           break;
       }

       // 加锁更新共享内存
       WaitForSingleObject(g_hMutex, INFINITE);

       if (device_type == "left") {
           // 更新左侧预置位
           strcpy_s(g_pSharedData->left_preset, sizeof(g_pSharedData->left_preset), std::to_string(current_preset).c_str());

           // 计算并更新右侧联动目标
           g_pSharedData->right_linkage_target = CalculateRightLinkageTarget(current_preset);

           // 设置左侧更新标志
           g_pSharedData->left_updated = true;

           // 在控制台显示确认信息
           std::cout << "已设置左侧预置位为：" << current_preset;
           if (g_pSharedData->right_linkage_target != 0) {
               std::cout << " → 联动要求右侧切换到预置位：" << g_pSharedData->right_linkage_target;
           }
           std::cout << std::endl;
       }
       else { // right设备
           // 更新右侧预置位
           strcpy_s(g_pSharedData->right_preset, sizeof(g_pSharedData->right_preset), std::to_string(current_preset).c_str());

           // 计算并更新左侧联动目标
           g_pSharedData->left_linkage_target = CalculateLeftLinkageTarget(current_preset);

           // 设置右侧更新标志
           g_pSharedData->right_updated = true;

           // 在控制台显示确认信息
           std::cout << "已设置右侧预置位为：" << current_preset;
           if (g_pSharedData->left_linkage_target != 0) {
               std::cout << " → 联动要求左侧切换到预置位：" << g_pSharedData->left_linkage_target;
           }
           std::cout << std::endl;
       }

       ReleaseMutex(g_hMutex);

       // 清空输入行
       std::cout << "\n";
   }

   // 7. 等待观察者线程结束
   g_exit_program = true;
   if (observer.joinable()) {
       observer.join();
   }

   // 8. 释放资源
   ReleaseSharedMemory(hMapFile, g_pSharedData);
   if (g_hMutex) CloseHandle(g_hMutex);

   std::cout << "\n程序已退出！" << std::endl;
   return 0;
}

