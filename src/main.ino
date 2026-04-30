// 功能说明
// wifi连接无阻塞,NTP时间同步无阻塞,屏幕显示无阻塞；
// NTP每2小时与NTP服务器同步一次；
// 添加Deep Sleep功能,在进入规定时间段后,关闭屏幕,停止lvgl输出,关闭除了rtc外的一切功能,进入深睡眠;
// 深睡眠定义时间段精确到分钟;
// 深睡眠过程中不需要联网时尽量保持屏幕关闭,过了设定时间段立即恢复功能;
// 如果不是通过RTC唤醒的情况（比如断电后来电）,如果还在规定深睡眠时间段内,迟5分钟在运行进入深睡眠程序;
// 🟢注意:(如果不断电,只通过reset pin复位（比如串口）,仍然不会开启屏幕,会根据RTC中的数据判断是否再次进入深睡眠)
// 这5分钟所有功能正常运行,屏幕正常输出,如果加上5分钟超过了深睡眠设定时间则不进入深睡眠,
// 由于8266定时精度有误差,深睡眠前半小时强制同步NTP时间一次,尽可能精确的进入深睡眠,
// 进入深睡眠时的定时长度=当前需要睡眠的时长-(当前需要睡眠的时长*2%),
// 注意只在每次NTP同步后确定睡眠多久时减一次2%，排除8266RTC计时长度可能不够导致的多次唤醒中不联网的情况下补偿
// 让8266可以提前醒过来校对时间,尽可能能精确的唤醒，
// 设备后续的DeviceState状态切换先判断NTP是否同步成功，否则跳过DeviceState状态切换

// 🟢 可修改区域：
// #define DEBUG_ENABLED        // 串口查看基本信息可能刷屏
// #define DEBUG_ENABLED_0      // 串口查看基本信息可能刷屏
// #define DEBUG_ENABLED_TIME   // 串口查看时间相关信息
// #define DEBUG_ENABLED_RAM    // 串口查看Task_cb中内存占用情况
// #define DEBUG_ENABLED_CPU    // 串口查看loop中cpu占用率
// #define DEBUG_ENABLED_DATA   // 串口查看获取NetData的数据
// #define DEBUG_ENABLED_WIFI   // 取消注释以启用WiFi调试信息

const char *ssid = "AX";           // 连接WiFi名（此处使用AX为示例）
                                   // 请将您需要连接的WiFi名填入引号中
const char *password = "12345678"; // 连接WiFi密码（此处使用12345678为示例）
// NetData服务器配置
#define NETDATA_SERVER_IP "192.168.10.1"  // 定义被监控的NetData服务器地址
#define NETDATA_SERVER_PORT 19999         // NetData服务器端口
//修改数据获取接口的相关用AI搜索相关代码
//下面是路由器cpu温度接口关键词 
#define CHART_NET         "net.wan"
#define CHART_CPU         "system.cpu"
#define CHART_MEM         "mem.available"
#define CHART_TEMP        "sensors.temp_thermal_zone0_thermal_thermal_zone0_thermal_zone0"
// 维度过滤数据方向
#define DIM_RX            "received"
#define DIM_TX            "sent"
// 注意：维度名必须与 NetData 中实际名称一致，
// 请根据解析函数（ parseBatchArrayResponse）中使用的维度名调整。
// 当前解析中使用的维度为：
//   CPU: "system"  |  网络: "received","sent"  |  内存: 含 "avail"  |  温度: 含 "temp"
// 下面字符串包含了这些关键字的常见精确名称，如果与实际不符，请通过串口输出一次完整响应调整。
// 显示不正确可尝试注销parseBatchArrayResponse中的下面代码，然后查询维度数据修复
// reqRes += "&dimensions=received,sent,temp,system,avail";
// 查看维度名称，可在parseBatchArrayResponse函数中的串口日志方式实时jsonStr；

// 被监控的路由器Ram大小单位MB
#define CHART_MEM_X   1024.0
//路由器中设备hostname
#define ROUTERMONITORPLUS_8266_HOSTNAME "RouterMonitorPlus"
//#define ROUTERMONITORPLUS_8266_HOSTNAME "RouterMonitor"
     
// 深睡眠总开关： true  启用深睡眠功能， false  完全禁用深睡眠
#define DEEP_SLEEP_ENABLED false
// 深睡眠时间段（24h制,精确到分钟） 定时
constexpr uint8_t SLEEP_START_HOUR = 21; // 开始：21:20
constexpr uint8_t SLEEP_START_MIN = 20;
constexpr uint8_t SLEEP_END_HOUR = 07;   // 结束：07:00
constexpr uint8_t SLEEP_END_MIN = 00;
// 宽限期时限
constexpr uint32_t POST_POWERON_GRACE_MS = 5 * 60 * 1000UL; // 重新上电(不是Reset)后显示5min监控再进入深睡眠
// ┌─────────────────────────────────────────────────────────────────────────┐
// │    方便随时调整定时补偿比例或关闭补偿,建议2%,8266RTC计时器精度-5%左右,偏快   │
// └─────────────────────────────────────────────────────────────────────────┘
//具体多少可以打开#define DEBUG_ENABLED_0和正点原子串口调试助手的时间戳功能监控深睡眠一段时间查看
#define RTC_COMPENSATE_PERCENT 2 // 补偿 2 % 由于8266RTC计时器精度不够，所以提前2%的时间醒来校正时间
#define MIN_SLEEP_WINDOW_SEC 300//300 // 5 min,小于等于该值就不再睡
// 若希望关闭补偿,只需把 RTC_COMPENSATE_PERCENT 改成 0。
// 若需要更精细的校准，可把 RTC_COMPENSATE_PERCENT 设成实测值（如 6、7 等百分比值）。

// ===== 屏幕亮度配置 =====
// 注意：setBrightness() 函数中,值越小越亮,值越大越暗
// 有效范围：0-256,0最亮,256最暗（关闭）
#define SCREEN_BRIGHTNESS_NORMAL 230 // 正常使用时的亮度  修改正常使用时候的亮度
#define SCREEN_BRIGHTNESS_OFF 255    // 关闭背光（最暗）
#define SCREEN_BRIGHTNESS_MAX 0      // 最亮（不建议长时间使用）
#define SCREEN_BRIGHTNESS_MIN 255    // 最暗（等同于关闭）
// 网络请求计数
static uint8_t requestCycleCount = 0; 
// 监控数据更新频率(秒)
#define DEFAULT_BATCH_INTERVAL 1000       // 批量请求间隔基准值（默认 1000ms）
// ===== 控制全量数据请求的间隔次数 =====
// 每 FULL_REQUEST_INTERVAL 次请求中，第1次获取全部数据，剩余次数仅获取 CPU + 网速
// 默认值 5 表示每5秒才有1秒请求内存和温度，其余4秒只请求变化快的指标
#define FULL_REQUEST_INTERVAL 5
// 全局变量 task_cb 完成时刻标记
unsigned long lastUiUpdateDone = 0;   // task_cb 完成时刻
unsigned long batchRequestInterval = DEFAULT_BATCH_INTERVAL; // 动态调整的请求间隔
const unsigned long UI_WIFI_MARGIN = 100; // UI 完成到允许发起请求的最小间隔(ms)

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <string>
#include <ESP8266WiFi.h>
#include "NetData.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

// ==== 全局变量 ====
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "cn.pool.ntp.org", 8 * 3600); // 东八区(+8小时)
// 全局变量
bool ntpInitialized = false;  // NTP初始化标志
bool rtcNormalWakeup = false; // 标记是否为RTC正常唤醒（睡眠周期完成）
// ==== 新增：时间有效性检查 ====
#define MIN_VALID_YEAR 2020 // 最小有效年份（2020年及以后）

// ===== WiFi发射功率配置 =====
// 有效范围：0.0 dBm 到 20.5 dBm，步进 0.25 dBm
// 推荐值：
//   20.5 dBm - 最大功率（默认值）
//   16.5 dBm - 中等功率【推荐用于桥接场景，降低CPU负担】
//   12.0 dBm - 低功率（信号良好时）
//   8.5 dBm  - 最低功率
#define WIFI_TX_POWER_DBM 16.5

// 是否启用动态功率调整（根据RSSI自动调整）
#define ENABLE_DYNAMIC_TX_POWER true
#define RSSI_GOOD_THRESHOLD -65    // RSSI高于此值降低功率
#define RSSI_MEDIUM_THRESHOLD -75  // RSSI低于此值提高功率
// ===== 自适应 RSSI 检查频率 =====
static unsigned long lastRSSICheck = 0;
const unsigned long FAST_CHECK_INTERVAL = 20000;  // 快速阶段：20秒
const unsigned long SLOW_CHECK_INTERVAL = 60000;  // 稳定阶段：60秒
const unsigned long FAST_PHASE_DURATION = 180000; // 前3分钟为快速阶段

// ==== 新增：NTP同步控制 ====
unsigned long lastNTPSyncTime = 0;
const unsigned long NTP_SYNC_INTERVAL = 2 * 3600 * 1000; // 2小时同步一次

// 在此处列出SD2小电视真正用到的引脚
// ：TFT_BL = 5，TFT_DC = 4，LED = 2，DC=0， RES=2， SCK=14, MOSI=13, 其余引脚区别设置为高阻
static const uint8_t IN_USE_PINS[] = { 5, 4, 2, 0, 14, 13};   // ← 按需增删
static const size_t  IN_USE_COUNT = sizeof(IN_USE_PINS) / sizeof(IN_USE_PINS[0]);

// ===== NTP同步状态枚举 =====
// 注意：这些状态用于管理非阻塞的NTP同步过程
enum NTPSyncState
{
    NTP_STATE_IDLE,      // 空闲状态,未开始同步
    NTP_STATE_SYNCING,   // 同步中,等待NTP响应
    NTP_STATE_COMPLETED, // 同步完成,时间有效
    NTP_STATE_FAILED     // 同步失败
};

// ===== 新增：NTP同步相关全局变量 =====
NTPSyncState ntpState = NTP_STATE_IDLE;       // NTP同步状态
unsigned long ntpSyncStartTime = 0;           // NTP同步开始时间
const unsigned long NTP_SYNC_TIMEOUT = 10000; // NTP同步超时时间(10秒)

// ===== 新增：统一设备状态枚举 =====
enum DeviceState
{
    STATE_BOOT,         // 启动状态
    STATE_NORMAL,       // 正常运行状态
    STATE_GRACE_PERIOD, // 宽限期状态（断电后5分钟）
    STATE_PRE_SLEEP,    // 准备进入深睡眠状态
    STATE_DEEP_SLEEP    // 深睡眠状态
};

// ===== 替换原有分散的状态变量 =====
DeviceState deviceState = STATE_BOOT; // 当前设备状态
unsigned long stateStartTime = 0;     // 状态开始时间
unsigned long gracePeriodEnd = 0;     // 宽限期结束时间

// 在现有常量定义后添加
const unsigned long NTP_VALID_DURATION = 30 * 60 * 1000;  // 30分钟
const unsigned long FORCE_SYNC_INTERVAL = 30 * 60 * 1000; // 深睡眠前半小时强制同步

using namespace std;
bool isLoggedIn = false; // 新增：登录状态标志
unsigned long lastRefreshTime = 0;
const unsigned long LOGGED_IN_REFRESH_INTERVAL = 499; // 登录后屏幕刷新间隔(ms)待机时间 不建议修改

// ==== 新增：WiFi连接状态管理 ====
enum WiFiConnectionState
{
    WIFI_STATE_DISCONNECTED, // WiFi断开状态
    WIFI_STATE_CONNECTING,   // WiFi连接中状态
    WIFI_STATE_CONNECTED     // WiFi已连接状态
};

// ===== 新增：时间检查结果结构体 =====
struct TimeCheckResult
{
    bool inSleepWindow;    // 是否在睡眠窗口内
    uint32_t sleepSeconds; // 需要睡眠的秒数(0表示不需要睡眠)
    uint8_t currentHour;   // 当前小时
    uint8_t currentMinute; // 当前分钟
};

// // ===== 屏幕刷新标志，用于屏蔽屏幕刷新期间的 WiFi 操作 =====
// bool screenRefreshing = false;

WiFiConnectionState wifiState = WIFI_STATE_DISCONNECTED; // 初始状态为断开
unsigned long wifiConnectionStartTime = 0;               // 连接开始时间
const unsigned long WIFI_CONNECTION_TIMEOUT = 10000;     // 10秒连接超时(毫秒)
unsigned long lastWiFiCheck = 0;                         // 上次检查WiFi状态的时间
const unsigned long WIFI_RECONNECT_INTERVAL = 2000;      // 2秒重连间隔
int wifiReconnectAttempts = 0;                           // 重连尝试次数
const int MAX_WIFI_RECONNECT_ATTEMPTS = 30;              // 最大重试次数

// ===== MAC地址修改相关变量 =====
bool macAddressChanged = false;      // 标记是否已经更改过MAC地址
bool hasEverConnected = false;       // 标记是否曾经成功连接过WiFi
const int MAC_CHANGE_ATTEMPTS = 15;  // 连接失败15次后更改MAC
// 修改RTC内存读取方式
struct RTCData
{
    uint8_t marker;           // 唤醒标记 (0xA5)
    uint8_t compensated;      // 是否已应用补偿 (0x01=已应用)
    uint8_t padding[2];       // 填充字节，确保4字节对齐
    uint32_t remaining;       // 剩余睡眠时间
} __attribute__((packed, aligned(4)));

// 修改RTC内存地址常量
constexpr uint32_t RTC_ADDR = 64; // 使用64字节偏移，避免与系统冲突
                                // ESP8266 RTC user memory 起始地址（4字节对齐）

unsigned long lastWiFiTimeCheck = 0;
const unsigned long WIFI_TIME_CHECK_INTERVAL = 6000; // 6秒检查一次

// 在全局变量区域添加
bool forceNTPSyncBeforeSleep = false;
unsigned long forceSyncStartTime = 0;

// 新增：屏幕状态控制
bool shouldDisplay = true; // 控制是否显示内容
bool uiReady = false;      // 控制UI是否初始化

// extern lv_font_t my_font_name;
LV_FONT_DECLARE(tencent_w7_22)
LV_FONT_DECLARE(tencent_w7_24)

TFT_eSPI tft = TFT_eSPI(); /* TFT instance */
static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

// 定义页面
static lv_obj_t *login_page = NULL;
static lv_obj_t *monitor_page = NULL;

// basic variables 基本变量
static uint8_t test_data = 0;
// static lv_obj_t* label1;
static lv_obj_t *upload_label;
// static lv_obj_t *down_label;
static lv_obj_t *up_speed_label;
static lv_obj_t *up_speed_unit_label;
static lv_obj_t *down_speed_label;
static lv_obj_t *down_speed_unit_label;
static lv_obj_t *cpu_bar;
static lv_obj_t *cpu_value_label;
static lv_obj_t *mem_bar;
static lv_obj_t *mem_value_label;
static lv_obj_t *temp_value_label;
static lv_obj_t *temperature_arc;
static lv_obj_t *ip_label;
static lv_style_t arc_indic_style;
static lv_obj_t *chart;

static lv_chart_series_t *ser1;
static lv_chart_series_t *ser2;

static lv_obj_t *wifi_status_led = NULL;  // WiFi状态指示灯

NetChartData netChartData;

// ===== 异步数据请求调度器 =====
enum DataRequestPhase {
    REQ_IDLE,           // 空闲，未发起请求
    REQ_BATCH           // 批量请求中（新增）
};

DataRequestPhase currentRequestPhase = REQ_IDLE;
unsigned long lastDataRequestTime = 0;

// 数据新鲜度标志
bool newCPUData = false;
bool newMemData = false;
bool newTempData = false;
bool newNetRxData = false;
bool newNetTxData = false;

// 声明全局 httpCtx（在 NetData.h 中已 extern，这里定义）
AsyncHttpContext httpCtx;

lv_coord_t up_speed_max = 0;
lv_coord_t down_speed_max = 0;
// 监测数值
double up_speed;
double down_speed;
double cpu_usage;
double mem_usage;
double temp_value;
lv_coord_t upload_serise[10] = {0};
lv_coord_t download_serise[10] = {0};

// ===== 前置声明 =====
// 函数声明
bool isTimeValid();
void UI_init();
void setBrightness(int value);
bool startAsyncNTPSync(bool force);
bool isTimeInSleepWindow(uint8_t hour, uint8_t minute);
void enterDeepSleep(uint32_t seconds);
void checkNTPSyncStatus();
uint32_t secondsToEndOfSleepWindow(uint8_t hour, uint8_t minute);
void actualEnterDeepSleep(uint32_t, bool alreadyCompensated);
void changeDeviceState(DeviceState newState);
TimeCheckResult checkSleepTime();
void handleTimeCheckResult(TimeCheckResult result);
void setDisplayState(bool enable);
void handleWiFiConnection();
void setupPages();
void initLoginPage();
void setUnusedPinsHiZ();
static inline bool pinInUse(uint8_t p);
lv_coord_t updateNetSeries(lv_coord_t *series, double speed);
void updateNetworkInfoLabel();
void updateChartRange();
bool connectWiFi(bool forceFullReset = false);
static void task_cb(lv_task_t *task);

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(lv_log_level_t level, const char *file, uint32_t line, const char *dsc, const char *params)
{

    Serial.printf("%s@%d->%s [%s]\r\n", file, line, dsc, params);
    Serial.flush();
}
#endif

#ifdef DEBUG_ENABLED_TIME
// ==== 新增函数：格式化时间输出 ====
void printFormattedTime()
{
    // 获取当前时间戳
    time_t epochTime = timeClient.getEpochTime();

    // 转换为时间结构体
    struct tm *ptm = gmtime(&epochTime);
    // 格式化输出时间
    Serial.printf("time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
                  ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
}
#endif

#ifdef DEBUG_ENABLED_CPU
unsigned long loopStartCycle = 0;
unsigned long loopEndCycle = 0;
unsigned long loopCycleCount = 0;
unsigned long loopCounter = 0;
unsigned long lastCpuReportTime = 0;
const unsigned long CPU_REPORT_INTERVAL = 5000; // 每5秒报告一次

// CPU 使用率分析函数
void analyzeCpuUsage() {
    loopEndCycle = ESP.getCycleCount();
    loopCycleCount += (loopEndCycle - loopStartCycle);
    loopCounter++;
    
    unsigned long currentTime = millis();
    if (currentTime - lastCpuReportTime >= CPU_REPORT_INTERVAL) {
        // 计算平均每个loop的CPU周期数
        unsigned long avgCyclesPerLoop = loopCycleCount / loopCounter;
        
        // 计算CPU使用率百分比
        // ESP8266运行在80MHz，所以每秒有80,000,000个周期
        // 5秒内总可用周期 = 5 * 80,000,000 = 400,000,000
        float cpuUsagePercent = (float(loopCycleCount) / 400000000.0) * 100.0;
        
        Serial.printf("[CPU] Loops: %lu, Avg cycles/loop: %lu, CPU Usage: %.2f%%\n",
                     loopCounter, avgCyclesPerLoop, cpuUsagePercent);
        
        // 重置计数器
        loopCycleCount = 0;
        loopCounter = 0;
        lastCpuReportTime = currentTime;
    }
    
    // 为下一个loop记录开始周期
    loopStartCycle = ESP.getCycleCount();
}
#endif

#if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
#define WIFI_POWER_LOG(oldP, newP, rssi, reason) \
    do { \
        Serial.printf("[WiFi-PWR] TTL=%lu | RSSI=%d | %s %.1f->%.1f dBm | Reason: %s\n", \
                     millis(), rssi, \
                     oldP < newP ? "UP" : (oldP > newP ? "DOWN" : "  KEEP"), \
                     oldP, newP, reason); \
    } while(0)
#else
#define WIFI_POWER_LOG(oldP, newP, rssi, reason) ((void)0)
#endif

// ===== 设置WiFi发射功率 =====
// power_dBm: 0.0 ~ 20.5 dBm
void setWiFiTxPower(float power_dBm) {
    // 限制功率范围
    if (power_dBm < 0.0) power_dBm = 0.0;
    if (power_dBm > 20.5) power_dBm = 20.5;
    
    WiFi.setOutputPower(power_dBm);
    
    #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
    // 【新增】TTL日志：功率设置确认
    Serial.printf("[WiFi-PWR] TTL=%lu | setOutputPower(%.1f dBm) OK\n", millis(), power_dBm);
    #endif
}

// 动态调整发射功率（基于RSSI）
// ===== 优化版动态功率调整 =====
// 将初始功率和默认功率分离，currentPower 初始为 WIFI_TX_POWER_DBM。
// 在 -60~-70dBm 区间，目标设为 16.5，使功率逐步回归默认值。
// 增加功率渐变步长 POWER_STEP = 1.0，避免跳变。
// 滞后计数改为 3 次，更稳定。当连接后检查频率为 20 秒或 60 秒时，约 1~3 分钟完成一次功率切换，合理。
void dynamicAdjustTxPower(int currentRSSI) {
    if (!ENABLE_DYNAMIC_TX_POWER) return;

    static float currentPower = WIFI_TX_POWER_DBM;
    static float targetPower = currentPower;
    static int stableCount = 0;          // 目标未变次数
    static int lastRSSI = currentRSSI;   // 用于判断趋势
    const int HYSTERESIS_COUNT = 3;      // 连续 3 次确认才切换
    const float POWER_STEP = 1.0;        // 每次最多调整 1 dBm（平滑）
    const float WEAK_MAX_POWER = 20.5;
    const float MEDIUM_POWER = 18.5;
    const float DEFAULT_POWER = 16.5;
    const float LOW_POWER = 12.0;

    // 根据 RSSI 计算理想目标功率（不考虑滞后）
    float idealPower;
    if (currentRSSI > RSSI_GOOD_THRESHOLD + 5) {         // > -55 dBm
        idealPower = LOW_POWER;
    } else if (currentRSSI > RSSI_GOOD_THRESHOLD) {      // -55 ~ -60 dBm
        idealPower = LOW_POWER;
    } else if (currentRSSI >= RSSI_MEDIUM_THRESHOLD) {   // -60 ~ -70 dBm
        idealPower = DEFAULT_POWER;                       // 向默认功率回归
    } else if (currentRSSI >= RSSI_MEDIUM_THRESHOLD - 5) {// -70 ~ -75 dBm
        idealPower = MEDIUM_POWER;
    } else {                                              // < -75 dBm
        idealPower = WEAK_MAX_POWER;
    }

    // 滞后处理：只有当 idealPower 连续 HYSTERESIS_COUNT 次不变，才认定为稳定目标
    if (idealPower != targetPower) {
        targetPower = idealPower;
        stableCount = 0;
    } else {
        stableCount++;
    }

    // 只有目标稳定且与当前功率不同时，才执行调整
    if (stableCount >= HYSTERESIS_COUNT && abs(currentPower - targetPower) > 0.1) {
        float newPower;
        if (targetPower > currentPower) {
            newPower = currentPower + POWER_STEP;
            if (newPower > targetPower) newPower = targetPower;
        } else {
            newPower = currentPower - POWER_STEP;
            if (newPower < targetPower) newPower = targetPower;
        }

        WIFI_POWER_LOG(currentPower, newPower, currentRSSI, "RSSI-based");
        setWiFiTxPower(newPower);
        currentPower = newPower;
        stableCount = 0;   // 重置计数，等待下一次稳定
    }

    lastRSSI = currentRSSI;
}

// ===== 新增：状态转换函数 =====
void changeDeviceState(DeviceState newState)
{
#ifdef DEBUG_ENABLED
    Serial.printf("State change: %d -> %d\n", deviceState, newState);
#endif

    // 处理状态退出逻辑
    switch (deviceState)
    {
    case STATE_NORMAL:
        // 正常状态退出逻辑
        break;
    case STATE_GRACE_PERIOD:
        // 宽限期退出逻辑
        break;
    case STATE_PRE_SLEEP:
        // 准备睡眠退出逻辑
        break;
    default:
        break;
    }

    // 更新状态和开始时间
    deviceState = newState;
    stateStartTime = millis();

    // 处理状态进入逻辑
    switch (deviceState)
    {
    case STATE_GRACE_PERIOD:
        gracePeriodEnd = millis() + POST_POWERON_GRACE_MS;
#ifdef DEBUG_ENABLED_0
        Serial.println("Entering grace period (5 minutes)\n");
#endif
        break;
    case STATE_PRE_SLEEP:
// 准备进入深睡眠
#ifdef DEBUG_ENABLED_0
        Serial.println("Preparing for deep sleep\n");
#endif
        break;
    case STATE_DEEP_SLEEP:
        // 实际进入深睡眠的处理在enterDeepSleep中
        break;
    default:
        break;
    }
}

// ===== 新增：统一时间检查函数 =====
TimeCheckResult checkSleepTime()
{
    TimeCheckResult result = {false, 0, 0, 0};

    // 只有NTP同步完成才能进行时间检查
    if (ntpState != NTP_STATE_COMPLETED)
    {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Cannot check sleep time: NTP not synchronized\n");
        #endif
        return result;
    }

    // ===== 删除这行！！！ =====
    // timeClient.update();  <-- 删掉！这会干扰异步同步
    // 直接读取已同步的时间（NTPClient 内部已缓存）
    result.currentHour = timeClient.getHours();
    result.currentMinute = timeClient.getMinutes();

    // 检查是否在睡眠窗口内
    result.inSleepWindow = isTimeInSleepWindow(result.currentHour, result.currentMinute);

    // 如果在睡眠窗口内，计算剩余睡眠时间
    if (result.inSleepWindow)
    {
        result.sleepSeconds = secondsToEndOfSleepWindow(result.currentHour, result.currentMinute);
    }

    #ifdef DEBUG_ENABLED
    Serial.printf("Time check: %02d:%02d, In window: %s, Sleep seconds: %lu\n",
                 result.currentHour, result.currentMinute,
                 result.inSleepWindow ? "Yes" : "No", result.sleepSeconds);
    #endif

    return result;
}

// ===== 新增：时间检查结果处理函数 =====
void handleTimeCheckResult(TimeCheckResult result)
{
    // 如果深睡眠总开关关闭，则始终认为不在睡眠窗口内
    if (!DEEP_SLEEP_ENABLED) {
        result.inSleepWindow = false;
        result.sleepSeconds = 0;
    }
    
    switch (deviceState){
    case STATE_BOOT:
        // 启动状态下发现处于睡眠窗口
        if (result.inSleepWindow) {
            // 如果是RTC正常唤醒，直接进入准备睡眠状态，跳过宽限期
            if (rtcNormalWakeup) {
                changeDeviceState(STATE_PRE_SLEEP);
                rtcNormalWakeup = false; // 清除标志
            } else {
                // 冷启动，进入宽限期
                changeDeviceState(STATE_GRACE_PERIOD);
            }
        } else {
            changeDeviceState(STATE_NORMAL);
            rtcNormalWakeup = false; // 清除标志
        }
        break;

    case STATE_GRACE_PERIOD:
        // 宽限期结束后检查是否仍需睡眠
        if (millis() >= gracePeriodEnd)
        {
            if (result.inSleepWindow && result.sleepSeconds > 0)
            {
                changeDeviceState(STATE_PRE_SLEEP);
            }
            else
            {
                changeDeviceState(STATE_NORMAL);
            }
        }
        break;

    case STATE_NORMAL:
        // 正常运行中检查到睡眠窗口，准备睡眠
        if (result.inSleepWindow && result.sleepSeconds > 0)
        {
            changeDeviceState(STATE_PRE_SLEEP);
        }
        break;

    case STATE_PRE_SLEEP:
        // 准备睡眠状态下，确认睡眠时间后进入深睡眠
        if (result.sleepSeconds > 0){
            enterDeepSleep(result.sleepSeconds);
        } else{
            changeDeviceState(STATE_NORMAL);
        }
        break;

    default:
        break;
    }
}

// ===== 统一显示控制函数 =====
// ===== 修改后的：统一显示控制函数 =====
void setDisplayState(bool enable)
{
    // 如果要关闭显示，但UI还未初始化，则无需任何操作，直接返回
    if (!enable && !uiReady) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Display OFF requested but UI not initialized, skipping.\n");
        #endif
        return;
    }

    shouldDisplay = enable;

    if (enable)
    {
        // 确保UI已初始化
        if (!uiReady)
        {
            UI_init(); // 注意：UI_init 内部会设置 uiReady = true
        }
        // 打开显示和背光
        tft.writecommand(TFT_DISPON);
        delay(1);
        setBrightness(SCREEN_BRIGHTNESS_NORMAL);
#ifdef DEBUG_ENABLED_0
        Serial.println("Display turned ON\n");
#endif
    }
    else
    {
        // 只有UI准备好了，才需要执行反初始化流程
        // 关闭LVGL和显示
        lv_deinit();
        delay(1);
        tft.fillScreen(TFT_BLACK);
        delay(1);
        // 额外发送关闭命令确保屏幕完全关闭
        tft.writecommand(TFT_SLPIN);  // 进入睡眠模式
        delay(1);
        tft.writecommand(TFT_DISPOFF);
        delay(1);
        setBrightness(SCREEN_BRIGHTNESS_OFF);
        pinMode(TFT_BL, INPUT); // 设置为高阻态，减少功耗
        delay(1);
        uiReady = false; // 标记UI需要重新初始化
#ifdef DEBUG_ENABLED_0
        Serial.println("Display turned OFF\n");
#endif
    }
}

// ===== 新增：异步NTP同步启动函数 =====
/**
 * 启动异步NTP同步
 * @param force 是否强制同步（忽略同步间隔）
 * @return 成功启动同步返回true,否则返回false
 */
bool startAsyncNTPSync(bool force = false)
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    // 非强制模式，检查是否需要同步
    if (!force && ntpState == NTP_STATE_COMPLETED &&
        millis() - lastNTPSyncTime < NTP_SYNC_INTERVAL)
    {
        return true; // 时间还新鲜，不用同步
    }

    // 如果正在同步中，不重复启动
    if (ntpState == NTP_STATE_SYNCING)
        return true;

    // 启动异步同步
    ntpState = NTP_STATE_SYNCING;
    ntpSyncStartTime = millis();
    timeClient.forceUpdate(); // 发送 NTP 请求，非阻塞启动
    delay(1);

    #ifdef DEBUG_ENABLED_0
    Serial.println("NTP sync started\n");
    #endif

    return true;
}

// ===== 新增：NTP同步状态检查函数 =====
/**
 * 检查NTP同步状态（非阻塞）
 * 定期调用此函数来更新NTP同步状态
 */
void checkNTPSyncStatus()
{
    // 只在同步状态下检查
    if (ntpState != NTP_STATE_SYNCING)
        return;

    unsigned long currentTime = millis();

    // 检查超时（10秒）
    if (currentTime - ntpSyncStartTime > NTP_SYNC_TIMEOUT)
    {
#ifdef DEBUG_ENABLED_0
        Serial.println("NTP sync timeout\n");
#endif
        ntpState = NTP_STATE_FAILED;
        return;
    }
    // 检查NTP客户端是否完成更新
    if (timeClient.isTimeSet()){
        // 验证时间有效性
        if (isTimeValid()){
            ntpState = NTP_STATE_COMPLETED;
            lastNTPSyncTime = currentTime;
#ifdef DEBUG_ENABLED_TIME
            Serial.println("NTP sync completed successfully\n");
            printFormattedTime();
#endif
        }
        else{
            ntpState = NTP_STATE_FAILED;
#ifdef DEBUG_ENABLED_0
            Serial.println("NTP sync completed but time is invalid\n");
#endif
        }
    }
}

// ==== 新增：时间有效性检查函数 ====
bool isTimeValid()
{
    time_t epochTime = timeClient.getEpochTime();
    if (epochTime < 0)
    {
#ifdef DEBUG_ENABLED
        Serial.println("Invalid time: negative epoch time\n");
#endif
        return false;
    }

    // 转换为时间结构体
    struct tm *ptm = gmtime(&epochTime);
    int year = ptm->tm_year + 1900;

#ifdef DEBUG_ENABLED
    Serial.printf("Current NTP time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, ptm->tm_mon + 1, ptm->tm_mday,
                  ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
#endif

    // 检查年份是否在2020年及以后
    if (year < MIN_VALID_YEAR)
    {
#ifdef DEBUG_ENABLED
        Serial.printf("Invalid time: year %d is before minimum valid year %d\n",
                      year, MIN_VALID_YEAR);
#endif
        return false;
    }

    // 添加额外的合理性检查
    // 检查月份是否在1-12范围内
    if (ptm->tm_mon < 0 || ptm->tm_mon > 11)
    {
#ifdef DEBUG_ENABLED
        Serial.printf("Invalid time: month %d is out of range\n", ptm->tm_mon + 1);
#endif
        return false;
    }

    // 检查日期是否在1-31范围内
    if (ptm->tm_mday < 1 || ptm->tm_mday > 31)
    {
#ifdef DEBUG_ENABLED
        Serial.printf("Invalid time: day %d is out of range\n", ptm->tm_mday);
#endif
        return false;
    }

    // 检查小时是否在0-23范围内
    if (ptm->tm_hour < 0 || ptm->tm_hour > 23)
    {
#ifdef DEBUG_ENABLED
        Serial.printf("Invalid time: hour %d is out of range\n", ptm->tm_hour);
#endif
        return false;
    }

    // 检查分钟是否在0-59范围内
    if (ptm->tm_min < 0 || ptm->tm_min > 59)
    {
#ifdef DEBUG_ENABLED
        Serial.printf("Invalid time: minute %d is out of range\n", ptm->tm_min);
#endif
        return false;
    }
    return true;
}

// 屏幕亮度设置，value [0, 256] 越小越亮,越大越暗
void setBrightness(int value)
{
    pinMode(TFT_BL, INPUT);
    analogWrite(TFT_BL, value);
    pinMode(TFT_BL, OUTPUT);
}

// ==== 新增函数：计算到睡眠窗口结束的秒数 ====
uint32_t secondsToEndOfSleepWindow(uint8_t h, uint8_t m) {
    uint16_t now = h * 60 + m;
    uint16_t start = SLEEP_START_HOUR * 60 + SLEEP_START_MIN;
    uint16_t end = SLEEP_END_HOUR * 60 + SLEEP_END_MIN;
    
    // 处理跨午夜的情况
    if (start > end) {
        // 睡眠时间跨午夜
        if (now >= start) {
            // 当前时间在开始时间之后（晚上）
            return ((24 * 60 - now) + end) * 60UL;
        } else if (now < end) {
            // 当前时间在结束时间之前（早上）
            return (end - now) * 60UL;
        }
    } else {
        // 睡眠时间不跨午夜
        if (now >= start && now < end) {
            return (end - now) * 60UL;
        }
    }
    
    return 0; // 不在睡眠窗口内
}

// 计算距离下一次睡眠窗口开始的秒数（0 表示当前已在窗口内）
uint32_t secondsToNextSleepWindow(uint8_t hour, uint8_t minute) {
    uint16_t now = hour * 60 + minute;
    uint16_t start = SLEEP_START_HOUR * 60 + SLEEP_START_MIN;
    uint16_t end   = SLEEP_END_HOUR * 60 + SLEEP_END_MIN;

    if (start <= end) {                     // 不跨午夜
        if (now >= start && now < end) return 0;
        if (now < start) return (start - now) * 60UL;
        return ((24*60 - now) + start) * 60UL;
    } else {                                // 跨午夜
        if (now >= start || now < end) return 0;
        return (start - now) * 60UL;
    }
}

// ---------------- Deep-Sleep Helper -----------------
bool isTimeInSleepWindow(uint8_t h, uint8_t m)
{
    uint16_t now = h * 60 + m;
    uint16_t start = SLEEP_START_HOUR * 60 + SLEEP_START_MIN;
    uint16_t end = SLEEP_END_HOUR * 60 + SLEEP_END_MIN;
    // 处理跨午夜的时间段（如 23:30–06:30）
    if (start <= end)
        return (now >= start && now < end);
    else
        return (now >= start || now < end);
}

// markRTCWakeup函数
void markRTCWakeup(bool flag)
{
    RTCData rtc;
    rtc.marker = flag ? 0xA5 : 0x00;
    rtc.compensated = 0x00; // 重置补偿标志
    rtc.padding[0] = 0;
    rtc.padding[1] = 0;
    rtc.remaining = 0;
    ESP.rtcUserMemoryWrite(RTC_ADDR, (uint32_t *)&rtc, sizeof(rtc));
}

void actualEnterDeepSleep(uint32_t seconds, bool alreadyCompensated = false)
{
    // 总开关判断：若禁用深睡眠，则直接退出
    if (!DEEP_SLEEP_ENABLED) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Deep sleep disabled by global switch, aborting.\n");
        #endif
        markRTCWakeup(false);
        changeDeviceState(STATE_NORMAL);
        return;
    }
        // 健壮性检查：如果秒数为0，则直接返回，不进行睡眠
    if (seconds == 0) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("actualEnterDeepSleep called with 0 seconds, aborting.\n");
        #endif
        markRTCWakeup(false);
        changeDeviceState(STATE_NORMAL);
        return;
    }

    // 检查睡眠时间是否太短
    if (seconds <= MIN_SLEEP_WINDOW_SEC) {
        #ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep window too small: %lu s, skip deep-sleep\n", seconds);
        #endif
        markRTCWakeup(false);
        changeDeviceState(STATE_NORMAL);
        return;
    }

    #ifdef DEBUG_ENABLED_0
    Serial.printf("Actual deep sleep called with %u seconds, compensated: %s\n", 
                 seconds, alreadyCompensated ? "yes" : "no");
    #endif

    // 在actualEnterDeepSleep中，补偿计算需要更谨慎
    uint32_t compensated = seconds;
    if (!alreadyCompensated) {
        // 确保不会因为补偿导致数值溢出或为0
        if (seconds > 100) { // 只在足够大的值时应用补偿
            compensated = seconds * (100 - RTC_COMPENSATE_PERCENT) / 100;
            // 确保补偿后至少还有1秒
            if (compensated == 0) compensated = 1;
        }
    }
    
    // 检查补偿后的睡眠时间是否足够
    if (compensated <= MIN_SLEEP_WINDOW_SEC) {
        #ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep window too small after compensate: %lu s, skip deep-sleep\n", compensated);
        #endif
        markRTCWakeup(false);
        changeDeviceState(STATE_NORMAL);
        return;
    }

    // 定义单次最大睡眠时间（ESP8266 RTC 限制，约71分钟）
    const uint32_t MAX_SLEEP_SEC = 4260; // 71分钟 * 60秒 = 4260秒

    uint32_t actualSleep = compensated;
    uint32_t remaining = 0;

    // 核心逻辑修正：如果补偿后的时间超过硬件限制，则必须分割
    // 不再检查剩余时间是否小于 MIN_SLEEP_WINDOW_SEC
    if (compensated > MAX_SLEEP_SEC) {
        actualSleep = MAX_SLEEP_SEC;
        remaining = compensated - MAX_SLEEP_SEC;
        #ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep duration exceeds single sleep limit, splitting. First segment: %lu sec, Remaining: %lu sec\n", actualSleep, remaining);
        #endif
    }
    // 否则，不需要分割，一次性睡完
    else {
        actualSleep = compensated;
        remaining = 0;
        #ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep duration within limit. Single sleep: %lu sec\n", actualSleep);
        #endif
    }

    /* 注意：完全移除原来的“如果剩余时间太小则跳过”的检查块 */

    #ifdef DEBUG_ENABLED_0
    Serial.printf("Original sleep: %u sec, Compensated: %u sec\n", seconds, compensated);
    Serial.printf("Actual sleep: %u sec, Remaining: %u sec\n", actualSleep, remaining);
    #endif

    // 关闭显示
    setDisplayState(false);

    // 优化后：单次断开 + 强制射频关闭
    closeNetdataConnection();  // 关闭 NetData 连接
    delay(1);
    WiFi.disconnect(true);   // 清除连接状态
    delay(1);
    WiFi.mode(WIFI_OFF);     // 关闭 WiFi 模式
    delay(1);
    WiFi.forceSleepBegin();  // 强制射频进入睡眠（比 mode OFF 更省电）
    delay(1);              // 确保射频完全关闭
        
        // 更新WiFi状态
        wifiState = WIFI_STATE_DISCONNECTED;

    // 存储唤醒标记和剩余睡眠时间
    RTCData rtcData;
    rtcData.marker = 0xA5;
    rtcData.compensated = alreadyCompensated ? 0x01 : 0x00;  // 【位置2】根据实际状态设置
    rtcData.padding[0] = 0;
    rtcData.padding[1] = 0;
    rtcData.remaining = remaining;

    ESP.rtcUserMemoryWrite(RTC_ADDR, (uint32_t *)&rtcData, sizeof(rtcData));
    delay(1);

    // 【位置3】增强验证：检查所有关键字段
    RTCData verifyData;
    ESP.rtcUserMemoryRead(RTC_ADDR, (uint32_t *)&verifyData, sizeof(verifyData));

    bool writeOK = (verifyData.marker == 0xA5) &&
                (verifyData.compensated == rtcData.compensated) &&  // ✅ 新增：验证补偿标志
                (verifyData.remaining == remaining);

    if (!writeOK) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("RTC memory write verification failed!\n");
        Serial.printf("Expected: marker=0xA5, compensated=0x%02X, remaining=%lu\n",
                    rtcData.compensated, remaining);
        Serial.printf("Actual:   marker=0x%02X, compensated=0x%02X, remaining=%lu\n",
                    verifyData.marker, verifyData.compensated, verifyData.remaining);
        #endif
        markRTCWakeup(false);
        changeDeviceState(STATE_NORMAL);
        return;
    }

    #ifdef DEBUG_ENABLED_0
    Serial.printf("Enter deep-sleep for %lu seconds\n", actualSleep);
    Serial.flush();
    #endif

    // 增加稳定性措施
    delay(1); // 确保所有串口输出完成
    ESP.wdtDisable(); // 禁用看门狗
    yield(); // 处理 pending 事件

    // 进入深睡眠前最后检查
    #ifdef DEBUG_ENABLED_0
    Serial.printf("Entering deep sleep for %lu seconds\n", actualSleep);
    Serial.flush();
    // 增加稳定性延迟
    delay(1);
    #endif

    // 【位置8】使用看门狗安全的方式进入深睡眠
    // 在 deepSleep 前重新启用看门狗，但设置较长的超时
    ESP.wdtEnable(8000); // 8秒看门狗，足够 deepSleep 启动
    // 直接调用深睡眠函数（无法检查返回值）
    ESP.deepSleep(actualSleep * 1000000ULL, WAKE_RF_DEFAULT);

    // 备用方案：软件复位
    #ifdef DEBUG_ENABLED_0
    Serial.println("Deep sleep failed, performing software reset\n");
    Serial.flush();
    #endif

    // 如果深睡眠调用失败，这行代码可能会执行
    delay(1); // 额外延迟确保深睡眠启动
    ESP.restart(); // 如果深睡眠失败，重启设备
    
    // 如果深睡眠调用成功，以下代码理论上不会执行，但为防止 ESP.restart() 也失败：
    while (true) {
        // 但 ESP8266 的软件看门狗通常不会被 ISR 喂
        __asm__ __volatile__ ("nop"); // 空操作，不触发任务调度
    }
}

// 修改后的 enterDeepSleep 函数 - 只做决策
void enterDeepSleep(uint32_t seconds)
{
    if (!DEEP_SLEEP_ENABLED) {
        changeDeviceState(STATE_NORMAL);
        return;
    }
    // 检查是否需要强制同步NTP时间
    if (millis() - lastNTPSyncTime > FORCE_SYNC_INTERVAL)
    {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Scheduling force NTP sync before deep sleep\n");
        #endif
        forceNTPSyncBeforeSleep = true;
        forceSyncStartTime = millis();
        changeDeviceState(STATE_PRE_SLEEP);
        return; // 退出，等待异步同步完成
    }

    // 不需要强制同步，直接执行深睡眠
    // 这是第一次进入深睡眠，尚未应用补偿
    actualEnterDeepSleep(seconds, false);
}

// 页面初始化
void setupPages()
{
    setBrightness(SCREEN_BRIGHTNESS_NORMAL); // 设置屏幕亮度为正常值
    login_page = lv_cont_create(lv_scr_act(), NULL);
    lv_obj_set_size(login_page, 240, 240); // 设置容器大小
    lv_obj_set_style_local_bg_color(login_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_border_color(login_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_radius(login_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

    monitor_page = lv_cont_create(lv_scr_act(), NULL);
    lv_obj_set_size(monitor_page, 240, 240);

    lv_obj_set_hidden(login_page, false);
    lv_obj_set_hidden(monitor_page, true);
}

// 设置login_page显示组件
void initLoginPage()
{
    lv_style_t login_spinner_style;
    lv_style_init(&login_spinner_style);
    lv_style_set_line_width(&login_spinner_style, LV_STATE_DEFAULT, 5);
    lv_style_set_pad_left(&login_spinner_style, LV_STATE_DEFAULT, 5);
    lv_style_set_line_color(&login_spinner_style, LV_STATE_DEFAULT, lv_color_hex(0xff5d18));

    lv_obj_t *preload = lv_spinner_create(login_page, NULL);
    lv_obj_set_size(preload, 100, 100);
    lv_obj_align(preload, NULL, LV_ALIGN_CENTER, 0, 0);
}

// ===== 修改：更改MAC地址函数 =====
/**
 * 更改ESP8266的MAC地址为完全随机生成
 * 注意：ESP8266的MAC地址更改是临时的，重启后会恢复为原始MAC
 * 此函数会生成一个完全随机的MAC地址，确保符合MAC地址规范
 */
void changeMACAddress()
{
    if (macAddressChanged) return; // 已经更改过，不再更改
    
    // 生成完全随机的MAC地址
    uint8_t newMAC[6];
    
    // 使用真随机数种子（基于WiFi射频噪声）
    for (int i = 0; i < 6; i++) {
        newMAC[i] = (uint8_t)random(0, 256);
    }
    
    // 确保MAC地址符合规范：
    // 1. 第一个字节的最低有效位为0（单播地址）
    // 2. 第二个字节的最低有效位为0（全局唯一地址）
    newMAC[0] &= 0xFE; // 确保是单播地址（不是多播）
    newMAC[0] |= 0x02; // 设置为本地管理地址（非全球唯一）
    
    // 设置新的MAC地址
    wifi_set_macaddr(STATION_IF, &newMAC[0]);
    macAddressChanged = true;
    
    #ifdef DEBUG_ENABLED_WIFI
    Serial.printf("Changed MAC address to: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 newMAC[0], newMAC[1], newMAC[2],
                 newMAC[3], newMAC[4], newMAC[5]);
    #endif
}

// 修改connectWiFi()函数,添加状态检查和防止重复连接
bool connectWiFi(bool forceFullReset)
{  
    // 如果已经连接,直接返回
    if (wifiState == WIFI_STATE_CONNECTED) return true;

    // 如果正在连接中,检查是否超时
    if (wifiState == WIFI_STATE_CONNECTING) {
        if (millis() - wifiConnectionStartTime > WIFI_CONNECTION_TIMEOUT) {
            wifiState = WIFI_STATE_DISCONNECTED;
            wifiReconnectAttempts++;
        }
        return false;
    }
    // ===== 如果 MAC 已更改，强制硬重置 =====
    if (macAddressChanged) {
        forceFullReset = true;
    }

    // 关键修复：从未成功连接过，或强制重置，或重试超过3次，都用硬重置
    bool useHardReset = forceFullReset || !hasEverConnected || wifiReconnectAttempts >= 3;
    
    if (useHardReset) {
        WiFi.disconnect(true);   // true = 清除 SDK 缓存
        delay(1);
        WiFi.mode(WIFI_OFF);
        delay(1);
        WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_MODEM_SLEEP);// 或 WIFI_LIGHT_SLEEP
        // 只允许 STA 接口接收广播（默认就是 STA，但可以显式设置）
        //wifi_set_broadcast_if(STATION_IF);  // 仅 STA 接口接收广播（默认行为，平衡功能与功耗）或 0 表示关闭所有广播接收
        //wifi_set_broadcast_if(0);
        setWiFiTxPower(WIFI_TX_POWER_DBM);
        #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
        // 【新增】TTL日志：连接初始化
        Serial.printf("[WiFi-INIT] TTL=%lu | Hard reset, initial power=%.1f dBm\n", 
                     millis(), WIFI_TX_POWER_DBM);
        #endif
        WiFi.hostname("ROUTERMONITORPLUS_8266_HOSTNAME");
        WiFi.begin(ssid, password);  // 必须传参数
    } else {
        // 软重置：只有之前成功连接过才有效
        WiFi.disconnect(false);    // 不断开 SDK 缓存
        delay(1);
        WiFi.begin();  // 无参数，使用缓存
        delay(1);
        setWiFiTxPower(WIFI_TX_POWER_DBM);   // 【优化】软重置也重置到默认功率
        #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
                //TTL日志：软重置
        Serial.printf("[WiFi-INIT] TTL=%lu | Soft reset, keep current power\n", millis());
        #endif
    }

    wifiState = WIFI_STATE_CONNECTING;
    wifiConnectionStartTime = millis();
    // 不要在这里重置 wifiReconnectAttempts！
    
    return false;
}

// ===== 3： handleWiFiConnection() =====
void handleWiFiConnection()
{
    // // ===== 屏幕正在刷新时直接返回，避免 WiFi 操作干扰 =====
    // if (screenRefreshing) return;
    static unsigned long lastWiFiCheckTime = 0;
    unsigned long currentMillis = millis();

    // 【优化】降低检查频率：从 1 秒改为 6 秒（稳定时）
    // 连接中时保持 1 秒快速检查，连接后 6 秒检查一次
    unsigned long checkInterval = (wifiState == WIFI_STATE_CONNECTING) ? 1000 : 6000;
    if (currentMillis - lastWiFiCheckTime < checkInterval) {
        return;
    }
    lastWiFiCheckTime = currentMillis;

#if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
    static WiFiConnectionState lastWifiState = WIFI_STATE_DISCONNECTED;
    if (wifiState != lastWifiState) {
        Serial.printf("[WiFi] State changed: %d -> %d\n", lastWifiState, wifiState);
        lastWifiState = wifiState;
    }
#endif

    // ===== 睡眠状态处理（保持不变）=====
    if (deviceState == STATE_DEEP_SLEEP || deviceState == STATE_PRE_SLEEP)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            #ifdef DEBUG_ENABLED_0
            Serial.println("Disconnecting WiFi due to sleep state\n");
            #endif
            for (int i = 0; i < 3; i++) {
                WiFi.disconnect(true);
            }
            wifiState = WIFI_STATE_DISCONNECTED;
        }
        return;
    }

    // ===== NTP 同步完成后检查睡眠窗口（保持不变）=====
    if (ntpState == NTP_STATE_COMPLETED)
    {
        if (millis() - lastWiFiTimeCheck >= WIFI_TIME_CHECK_INTERVAL) {
            TimeCheckResult timeCheck = checkSleepTime();
            lastWiFiTimeCheck = millis();
            
            if (timeCheck.inSleepWindow &&
                (deviceState == STATE_PRE_SLEEP ||
                (deviceState == STATE_GRACE_PERIOD && millis() >= gracePeriodEnd)))
            {
                if (WiFi.status() == WL_CONNECTED)
                {
#if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
                    Serial.println("Disconnecting WiFi due to sleep window\n");
#endif
                    WiFi.disconnect();
                    wifiState = WIFI_STATE_DISCONNECTED;
                }
                return;
            }
        }
    }

    // ===== 状态机处理 =====
    switch (wifiState)
    {
    case WIFI_STATE_CONNECTING:
        // 检查连接是否成功
        if (WiFi.status() == WL_CONNECTED)
        {
            wifiState = WIFI_STATE_CONNECTED;
            wifiReconnectAttempts = 0;
            hasEverConnected = true;  // 标记已成功连接过

#if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
            Serial.println("WiFi connected!\n");
            Serial.print("IP address: \n");
            Serial.println(WiFi.localIP());
#endif

            // 初始化 NTP 客户端
            if (!ntpInitialized)
            {
                timeClient.begin();
                ntpInitialized = true;
#if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
                Serial.println("NTP client initialized.\n");
#endif
                startAsyncNTPSync(true);
            }
            
            if (ip_label && uiReady)
            {
                lv_label_set_text(ip_label, WiFi.localIP().toString().c_str());
            }
        }
        // 检查超时
        else if (millis() - wifiConnectionStartTime > WIFI_CONNECTION_TIMEOUT)
        {
#if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
            Serial.println("WiFi connection timeout\n");
#endif
            wifiState = WIFI_STATE_DISCONNECTED;
            wifiReconnectAttempts++;
            lastWiFiCheck = millis();
        }
        break;

        case WIFI_STATE_CONNECTED:{
            // 【优化】稳定性检查：连续 3 次断开才认为真断开
            static int disconnectCount = 0;
            if (WiFi.status() != WL_CONNECTED)
            {
                disconnectCount++;
    #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
                Serial.printf("[WiFi] TTL=%lu | Check failed (%d/3)\n", millis(), disconnectCount);
    #endif
                if (disconnectCount >= 3)
                {
    #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
                    Serial.printf("[WiFi] TTL=%lu | Connection confirmed lost\n", millis());
    #endif
                    wifiState = WIFI_STATE_DISCONNECTED;
                    disconnectCount = 0;
                    closeNetdataConnection();   // 立即关闭可能残留的 TCP 连接
                }
            }
            else{
                disconnectCount = 0;
            }

            unsigned long now = millis();
            unsigned long timeSinceConnect = (wifiState == WIFI_STATE_CONNECTED) ? (now - wifiConnectionStartTime) : 0;
            unsigned long checkInterval = (timeSinceConnect < FAST_PHASE_DURATION) ? FAST_CHECK_INTERVAL : SLOW_CHECK_INTERVAL;

            if (now - lastRSSICheck > checkInterval){
                lastRSSICheck = now;
                int wifiRSSI = WiFi.RSSI(); // 改为局部变量，避免全局污染

    #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
                Serial.printf("[WiFi-RSSI] TTL=%lu | Raw RSSI: %d dBm | Phase: %s\n",
                            now, wifiRSSI,
                            (timeSinceConnect < FAST_PHASE_DURATION) ? "FAST" : "STABLE");
    #endif
                dynamicAdjustTxPower(wifiRSSI);
            }
        }
        break;
    case WIFI_STATE_DISCONNECTED:
        if (millis() - lastWiFiCheck > WIFI_RECONNECT_INTERVAL) {
            if (wifiReconnectAttempts >= MAC_CHANGE_ATTEMPTS && !macAddressChanged) {
                changeMACAddress();
                wifiReconnectAttempts = 0;
                hasEverConnected = false;  // 强制走硬重置路径
                connectWiFi(true);
            } else {
                bool forceHard = (wifiReconnectAttempts >= 3);
                connectWiFi(forceHard);
            }
            lastWiFiCheck = millis();
        }
        break;
    }
}

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors(&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

/* Reading input device (simulated encoder here) */
bool read_encoder(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    static int32_t last_diff = 0;
    int32_t diff = 0;                   /* Dummy - no movement */
    int btn_state = LV_INDEV_STATE_REL; /* Dummy - no press */

    data->enc_diff = diff - last_diff;
    data->state = btn_state;

    last_diff = diff;

    return false;
}

lv_coord_t updateNetSeries(lv_coord_t *series, double speed)
{
    lv_coord_t local_max = series[0];
    for (int index = 0; index < 9; index++)
    {
        series[index] = series[index + 1];
        if (local_max < series[index])
        {
            local_max = series[index];
        }
    }
    series[9] = (lv_coord_t)speed;
    if (local_max < series[9])
        local_max = series[9];
#ifdef DEBUG_ENABLED
    Serial.print(speed);
    Serial.print("->");
    Serial.print(series[9]);
    Serial.print("    |");
    for (int i = 0; i < 10; i++)
    {
        Serial.print(series[i]);
        Serial.print(" ");
    }
    Serial.println();
#endif
    return local_max;
}

// sensors.temp_thermal_zone0_thermal_thermal_zone0

void updateNetworkInfoLabel()
{
    if (up_speed < 100.0)
    {
        // < 99.99 K/S
        lv_label_set_text_fmt(up_speed_label, "%.2f", up_speed);
        lv_label_set_text(up_speed_unit_label, "K/s");
    }
    else if (up_speed < 1000.0)
    {
        // 999.9 K/S
        lv_label_set_text_fmt(up_speed_label, "%.1f", up_speed);
        lv_label_set_text(up_speed_unit_label, "K/s");
    }
    else if (up_speed < 100000.0)
    {
        // 99.99 M/S
        up_speed /= 1024.0;
        lv_label_set_text_fmt(up_speed_label, "%.2f", up_speed);
        lv_label_set_text(up_speed_unit_label, "M/s");
    }
    else if (up_speed < 1000000.0)
    {
        // 999.9 M/S
        up_speed = up_speed / 1024.0;
        lv_label_set_text_fmt(up_speed_label, "%.1f", up_speed);
        lv_label_set_text(up_speed_unit_label, "M/s");
    }

    if (down_speed < 100.0)
    {
        // < 99.99 K/S
        lv_label_set_text_fmt(down_speed_label, "%.2f", down_speed);
        lv_label_set_text(down_speed_unit_label, "K/s");
    }
    else if (down_speed < 1000.0)
    {
        // 999.9 K/S
        lv_label_set_text_fmt(down_speed_label, "%.1f", down_speed);
        lv_label_set_text(down_speed_unit_label, "K/s");
    }
    else if (down_speed < 100000.0)
    {
        // 99.99 M/S
        down_speed /= 1024.0;
        lv_label_set_text_fmt(down_speed_label, "%.2f", down_speed);
        lv_label_set_text(down_speed_unit_label, "M/s");
    }
    else if (down_speed < 1000000.0)
    {
        // 999.9 M/S
        down_speed = down_speed / 1024.0;
        lv_label_set_text_fmt(down_speed_label, "%.1f", down_speed);
        lv_label_set_text(down_speed_unit_label, "M/s");
    }
}

void updateChartRange()
{
    lv_coord_t max_speed = max(down_speed_max, up_speed_max);
    max_speed = max(max_speed, (lv_coord_t)16);
    lv_chart_set_range(chart, 0, (lv_coord_t)(max_speed * 1.1));
}

// 定期任务回调函数（约每秒执行一次）
// 负责更新监控数据和UI显示
static void task_cb(lv_task_t *task)
{
    // if (isLoggedIn) {
    // delay(200);
    // }
    //screenRefreshing = true;
    #ifdef DEBUG_ENABLED_RAM
    uint32_t task_start = millis();
    static uint32_t last_time = 0;
    #endif

    // 统一的WiFi状态和IP显示管理
    if (ip_label && wifi_status_led) {
        static WiFiConnectionState lastDisplayedState = WIFI_STATE_DISCONNECTED;
        static String lastDisplayedText;
        static unsigned long lastUiUpdateTime = 0;
        
        // 每1秒检查一次是否需要更新UI
        if (millis() - lastUiUpdateTime > 1000) {
            lastUiUpdateTime = millis();
            
            // 根据状态生成显示文本
            String currentText;
            lv_color_t led_color;
            
            switch (wifiState) {
                case WIFI_STATE_CONNECTED:
                    led_color = LV_COLOR_GREEN;
                    currentText = WiFi.localIP().toString(); // 直接获取IP地址
                    break;
                case WIFI_STATE_CONNECTING:
                    led_color = LV_COLOR_YELLOW;
                    currentText = "Connecting...";
                    break;
                case WIFI_STATE_DISCONNECTED:
                default:
                    led_color = LV_COLOR_RED;
                    currentText = "WiFi Disconnected";
                    break;
            }
            
            // 检查状态或文本是否发生变化
            bool shouldUpdate = (wifiState != lastDisplayedState) || (currentText != lastDisplayedText);
            
            if (shouldUpdate) {
                // 应用更新
                lv_obj_set_style_local_bg_color(wifi_status_led, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, led_color);
                lv_label_set_text(ip_label, currentText.c_str());
                
                // 记录当前显示的状态和文本
                lastDisplayedState = wifiState;
                lastDisplayedText = currentText;
                
                #ifdef DEBUG_ENABLED_WIFI
                Serial.printf("[UI] WiFi state updated: %d, Text: %s\n", wifiState, currentText.c_str());
                #endif
            }
        }
    }

    // ===== 修改：只在WiFi连接成功时获取网络数据 =====
    // 降低部分数据获取频率
    if (wifiState == WIFI_STATE_CONNECTED) {
        if (newCPUData) {
            lv_bar_set_value(cpu_bar, cpu_usage, LV_ANIM_OFF);
            lv_label_set_text_fmt(cpu_value_label, "%2.1f%%", cpu_usage);
            newCPUData = false;
        }
        if (newMemData) {
            lv_bar_set_value(mem_bar, mem_usage, LV_ANIM_OFF);
            lv_label_set_text_fmt(mem_value_label, "%2.0f%%", mem_usage);
            newMemData = false;
        }
        if (newTempData) {
            lv_label_set_text_fmt(temp_value_label, "%2.0f°C", temp_value);
            //更新温度弧线
            uint16_t end_value = 120 + 300 * temp_value / 100.0f;
            lv_color_t arc_color = temp_value > 75 ? lv_color_hex(0xff5d18) : lv_color_hex(0x50ff7d);
            lv_style_set_line_color(&arc_indic_style, LV_STATE_DEFAULT, arc_color);
            lv_obj_add_style(temperature_arc, LV_ARC_PART_INDIC, &arc_indic_style);
            lv_arc_set_end_angle(temperature_arc, end_value);
            newTempData = false;
        }
    }

    // ===== 【新增】同步更新网络速度图表（约在此处插入） =====
    if (uiReady) {
        // 检查是否有新的速度数据到来
        bool needChartUpdate = false;
        if (newNetRxData) {
            needChartUpdate = true;
            newNetRxData = false;
        }
        if (newNetTxData) {
            needChartUpdate = true;
            newNetTxData = false;
        }

        if (needChartUpdate) {
            // 更新图表系列
            lv_chart_set_points(chart, ser2, download_serise);  // 下载（绿色）
            lv_chart_set_points(chart, ser1, upload_serise);    // 上传（红色）
            updateChartRange();                                 // 自适应 Y 轴范围
            lv_chart_refresh(chart);                            // 立即刷新显示
        }

        // 更新速度标签（原 task_cb 中已有此调用，可保留，确保标签也同步刷新）
    // ===== 修改：无论WiFi状态如何,都更新UI =====
    updateNetworkInfoLabel();
    }
    // lv_bar_set_value(cpu_bar, cpu_usage, LV_ANIM_OFF);
    // lv_label_set_text_fmt(cpu_value_label, "%2.1f%%", cpu_usage);
    // lv_bar_set_value(mem_bar, mem_usage, LV_ANIM_OFF);
    // lv_label_set_text_fmt(mem_value_label, "%2.0f%%", mem_usage);
    // lv_label_set_text_fmt(temp_value_label, "%2.0f°C", temp_value);

    // // 更新温度弧线
    // uint16_t end_value = 120 + 300 * temp_value / 100.0f;
    // lv_color_t arc_color = temp_value > 75 ? lv_color_hex(0xff5d18) : lv_color_hex(0x50ff7d);
    // lv_style_set_line_color(&arc_indic_style, LV_STATE_DEFAULT, arc_color);
    // lv_obj_add_style(temperature_arc, LV_ARC_PART_INDIC, &arc_indic_style);
    // lv_arc_set_end_angle(temperature_arc, end_value);

#ifdef DEBUG_ENABLED_RAM
    if (isLoggedIn)
    {
        // 调试信息输出
        uint32_t freeRamBytes = ESP.getFreeHeap();
        float freeRamKB = freeRamBytes / 1024.0f;
        Serial.printf("[RAM] Free: %.2f KB (%d bytes) \n", freeRamKB, freeRamBytes);

        // uint32_t freeHeap = ESP.getFreeHeap();
        // Serial.printf("Pre-sleep check: Free heap: %u bytes\n", freeHeap);

        // uint32_t task_duration = millis() - task_start;
        // if (last_time > 0)
        // {
        //     uint32_t cycle_time = task_start - last_time;
        //     float cpu_usage_percent = 100.0 * (float)task_duration / cycle_time;
        //     Serial.printf("[CPU] Usage: %.1f%% | Task: %dms | Cycle: %dms\n",
        //                 cpu_usage_percent, task_duration, cycle_time);
        // }
        // last_time = task_start;
        }
#endif
//screenRefreshing = false;
// 在 task_cb 时间戳计数
    // ===== 新增：记录 UI 刷新完成，并调整下一次请求间隔 =====
    lastUiUpdateDone = millis();

    if (wifiState == WIFI_STATE_CONNECTED && !(newCPUData || newNetRxData || newNetTxData)) {
        unsigned long now = millis();
        unsigned long nextTaskCbTime = now + 1000; // task_cb 自身周期 1000ms
        unsigned long idealRequestTime = now + 300;
        if (idealRequestTime + 200 < nextTaskCbTime) {
            batchRequestInterval = 300;
        } else {
            batchRequestInterval = (nextTaskCbTime + UI_WIFI_MARGIN) - now;
        }
    } else {
        batchRequestInterval = DEFAULT_BATCH_INTERVAL;   // <-- 这里用宏
    }
}

void UI_init(void)
{
    
    if (uiReady)
        return; // 防止重复初始化

    // -------- 常规 LVGL / TFT 初始化继续 --------
    // 屏幕已在 setup() 中初始化，此处仅配置 LVGL
    lv_init();

    #if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print); /* register print function for debugging */
    #endif

    tft.begin();        /* TFT init */
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(0); /* Landscape orientation */

    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);

    /*Initialize the display*/
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /*Initialize the (dummy) input device driver*/
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = read_encoder;
    lv_indev_drv_register(&indev_drv);

    setupPages();
    initLoginPage();

    lv_obj_t *bg;
    bg = lv_obj_create(monitor_page, NULL);
    lv_obj_clean_style_list(bg, LV_OBJ_PART_MAIN);
    lv_obj_set_style_local_bg_opa(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_100);
    lv_color_t bg_color = lv_color_hex(0x7381a2);
    // bg_color = lv_color_hex(0xecdd5c);
    lv_obj_set_style_local_bg_color(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, bg_color);
    lv_obj_set_size(bg, LV_HOR_RES_MAX, LV_VER_RES_MAX);

    // 显示ip地址
    ip_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(ip_label, WiFi.localIP().toString().c_str());
    // lv_label_set_text(ip_label, "192.168.100.199");
    lv_obj_set_pos(ip_label, 10, 220);

    // 创建指示灯对象（圆形）
    wifi_status_led = lv_obj_create(monitor_page, NULL);
    lv_obj_set_size(wifi_status_led, 15, 15);  // 设置大小为15x15
    lv_obj_set_pos(wifi_status_led, 135, 220); // 设置位置在IP地址右侧
    lv_obj_set_style_local_radius(wifi_status_led, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE); // 设置为圆形
    lv_obj_set_style_local_bg_color(wifi_status_led, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);   // 默认红色（断开状态）
    lv_obj_set_style_local_border_width(wifi_status_led, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);          // 无边框


    lv_obj_t *cont = lv_cont_create(monitor_page, NULL);
    lv_obj_set_auto_realign(cont, true); /*Auto realign when the size changes*/
    // lv_obj_align_origo(cont, NULL, LV_ALIGN_IN_TOP_LEFT, 120, 35);  /*This parametrs will be sued when realigned*/
    // lv_color_t cont_color = lv_color_hex(0x1a1d25);
    lv_color_t cont_color = lv_color_hex(0x081418);
    lv_obj_set_width(cont, 230);
    lv_obj_set_height(cont, 120);
    lv_obj_set_pos(cont, 5, 5);

    lv_cont_set_fit(cont, LV_FIT_TIGHT);
    lv_cont_set_layout(cont, LV_LAYOUT_COLUMN_MID);
    lv_obj_set_style_local_border_color(cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_bg_color(cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, cont_color);

    // Upload & Download Symbol
    static lv_style_t iconfont;
    lv_style_init(&iconfont);
    lv_style_set_text_font(&iconfont, LV_STATE_DEFAULT, &iconfont_symbol);

    upload_label = lv_label_create(monitor_page, NULL);
    lv_obj_add_style(upload_label, LV_LABEL_PART_MAIN, &iconfont);
    lv_label_set_text(upload_label, CUSTOM_SYMBOL_UPLOAD);
    lv_color_t speed_label_color = lv_color_hex(0x838a99);
    lv_obj_set_style_local_text_color(upload_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    lv_obj_set_pos(upload_label, 10, 18);

    lv_obj_t *down_label = lv_label_create(monitor_page, NULL);
    lv_obj_add_style(down_label, LV_LABEL_PART_MAIN, &iconfont);
    lv_label_set_text(down_label, CUSTOM_SYMBOL_DOWNLOAD);
    speed_label_color = lv_color_hex(0x838a99);
    lv_obj_set_style_local_text_color(down_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
    lv_obj_set_pos(down_label, 120, 18);

    // Upload & Download Speed Display
    static lv_style_t font_22;
    lv_style_init(&font_22);
    // lv_style_set_text_font(&font_22, LV_STATE_DEFAULT, &lv_font_montserrat_24);
    lv_style_set_text_font(&font_22, LV_STATE_DEFAULT, &tencent_w7_22);

    up_speed_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(up_speed_label, "56.78");
    lv_obj_add_style(up_speed_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(up_speed_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(up_speed_label, 30, 15);

    up_speed_unit_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(up_speed_unit_label, "K/S");
    lv_obj_set_style_local_text_color(up_speed_unit_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, speed_label_color);
    lv_obj_set_pos(up_speed_unit_label, 90, 18);

    down_speed_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(down_speed_label, "12.34");
    lv_obj_add_style(down_speed_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(down_speed_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(down_speed_label, 142, 15);

    down_speed_unit_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(down_speed_unit_label, "M/S");
    lv_obj_set_style_local_text_color(down_speed_unit_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, speed_label_color);
    lv_obj_set_pos(down_speed_unit_label, 202, 18);

    // 绘制曲线图
    /*Create a chart*/
    chart = lv_chart_create(monitor_page, NULL);
    lv_obj_set_size(chart, 220, 70);
    lv_obj_align(chart, NULL, LV_ALIGN_CENTER, 0, -40);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE); /*Show lines and points too*/
    lv_chart_set_range(chart, 0, 4096);
    lv_chart_set_point_count(chart, 10); // 设置显示点数
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    /*Add a faded are effect*/
    lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50); /*Max. opa.*/
    lv_obj_set_style_local_bg_grad_dir(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_set_style_local_bg_main_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255); /*Max opa on the top*/
    lv_obj_set_style_local_bg_grad_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);   /*Transparent on the bottom*/

    /*Add two data series*/
    ser1 = lv_chart_add_series(chart, LV_COLOR_RED);
    ser2 = lv_chart_add_series(chart, LV_COLOR_GREEN);

    // /*Directly set points on 'ser2'*/
    lv_chart_set_points(chart, ser2, download_serise);
    lv_chart_set_points(chart, ser1, upload_serise);

    lv_chart_refresh(chart); /*Required after direct set*/

    // 绘制进度条  CPU 占用
    lv_obj_t *cpu_title = lv_label_create(monitor_page, NULL);
    lv_label_set_text(cpu_title, "CPU");
    lv_obj_set_style_local_text_color(cpu_title, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(cpu_title, 5, 140);

    cpu_value_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(cpu_value_label, "34%");
    lv_obj_add_style(cpu_value_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(cpu_value_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(cpu_value_label, 85, 135);

    lv_color_t cpu_bar_indic_color = lv_color_hex(0x63d0fc);
    lv_color_t cpu_bar_bg_color = lv_color_hex(0x1e3644);
    cpu_bar = lv_bar_create(monitor_page, NULL);
    lv_obj_set_size(cpu_bar, 130, 10);
    lv_obj_set_pos(cpu_bar, 5, 160);

    lv_obj_set_style_local_bg_color(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cpu_bar_bg_color);
    lv_obj_set_style_local_bg_color(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cpu_bar_indic_color);
    lv_obj_set_style_local_border_width(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_width(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 2);

    lv_obj_set_style_local_border_color(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_color(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_side(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_style_local_radius(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_radius(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 0);

    // 绘制内存占用
    lv_obj_t *men_title = lv_label_create(monitor_page, NULL);
    lv_label_set_text(men_title, "Memory");
    lv_obj_set_style_local_text_color(men_title, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(men_title, 5, 180);

    mem_value_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(mem_value_label, "42%");
    lv_obj_add_style(mem_value_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(mem_value_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(mem_value_label, 85, 175);

    mem_bar = lv_bar_create(monitor_page, NULL);
    lv_obj_set_size(mem_bar, 130, 10);
    lv_obj_set_pos(mem_bar, 5, 200);
    lv_obj_set_style_local_bg_color(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cpu_bar_bg_color);
    lv_obj_set_style_local_bg_color(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cpu_bar_indic_color);
    lv_obj_set_style_local_border_width(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_width(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_side(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_style_local_radius(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_radius(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 0);

    // 绘制温度表盘
    static lv_style_t arc_style;
    lv_style_reset(&arc_style);
    lv_style_init(&arc_style);
    lv_style_set_bg_opa(&arc_style, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_border_opa(&arc_style, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_line_width(&arc_style, LV_STATE_DEFAULT, 100);
    lv_style_set_line_color(&arc_style, LV_STATE_DEFAULT, lv_color_hex(0x081418));
    lv_style_set_line_rounded(&arc_style, LV_STATE_DEFAULT, false);

    lv_style_init(&arc_indic_style);
    lv_style_set_line_width(&arc_indic_style, LV_STATE_DEFAULT, 5);
    lv_style_set_pad_left(&arc_indic_style, LV_STATE_DEFAULT, 5);
    // lv_style_set_line_color(&arc_indic_style, LV_STATE_DEFAULT, lv_color_hex(0x50ff7d));
    lv_style_set_line_color(&arc_indic_style, LV_STATE_DEFAULT, lv_color_hex(0xff5d18));
    // lv_style_set_line_rounded(&arc_indic_style, LV_STATE_DEFAULT, false);

    temperature_arc = lv_arc_create(monitor_page, NULL);
    lv_arc_set_bg_angles(temperature_arc, 0, 360);
    lv_arc_set_start_angle(temperature_arc, 120);
    lv_arc_set_end_angle(temperature_arc, 420);
    lv_obj_set_size(temperature_arc, 125, 125);
    lv_obj_set_pos(temperature_arc, 125, 120);
    lv_obj_add_style(temperature_arc, LV_ARC_PART_BG, &arc_style);
    lv_obj_add_style(temperature_arc, LV_ARC_PART_INDIC, &arc_indic_style);
    // lv_obj_align(temperature_arc, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 10, 10);

    static lv_style_t font_24;
    lv_style_init(&font_24);
    lv_style_set_text_font(&font_24, LV_STATE_DEFAULT, &tencent_w7_24);

    temp_value_label = lv_label_create(monitor_page, NULL);
    lv_label_set_text(temp_value_label, "72℃");
    lv_obj_add_style(temp_value_label, LV_LABEL_PART_MAIN, &font_24);
    lv_obj_set_style_local_text_color(temp_value_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_pos(temp_value_label, 160, 170);

    lv_task_t *t = lv_task_create(task_cb, 1000, LV_TASK_PRIO_MID, &test_data);
    uiReady = true;

    // 4. 全部搞定后,再亮屏
    tft.writecommand(TFT_DISPON);
    delay(1);
    setBrightness(SCREEN_BRIGHTNESS_NORMAL); // 打开背光
}

// 2. 判断一个 pin 是否被占用
static inline bool pinInUse(uint8_t p)
{
    for (size_t i = 0; i < IN_USE_COUNT; ++i)
        if (IN_USE_PINS[i] == p) return true;
    return false;
}

// 3. 在 setup() 最后调用
void setUnusedPinsHiZ()
{
#if !defined(DEBUG_ENABLED) && !defined(DEBUG_ENABLED_0) && \
    !defined(DEBUG_ENABLED_TIME) && !defined(DEBUG_ENABLED_RAM)\
    && !defined(DEBUG_ENABLED_CPU) && !defined(DEBUG_ENABLED_WIFI)\
    && !defined(DEBUG_ENABLED_DATA)
    

    // NodeMCU v2 可用 GPIO：0,1,2,3,4,5,12,13,14,15,16
    // （6-11 已接 Flash，不可动）
    for (uint8_t p : { 0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16 })
    {
        if (!pinInUse(p))
        {
            pinMode(p, INPUT);          // 高阻
            // 若担心浮动，可改为 INPUT_PULLDOWN_16 / INPUT_PULLUP
        }
    }
#endif
}

// ===== 修改：setup函数 =====
void setup()
{
    // //验证代码死循环私人手工使用，建议AI保留
    // pinMode(TFT_BL, OUTPUT);
    // digitalWrite(TFT_BL, LOW);

    // tft.begin();
    // tft.writecommand(TFT_SLPOUT);
    // // delay(1);
    // tft.writecommand(TFT_DISPOFF);
    // tft.fillScreen(TFT_BLACK);

    // // 初始化背光为关闭状态
    // for (int i = 0; i < 3; i++)
    // {
    //     setBrightness(SCREEN_BRIGHTNESS_OFF);
    //     delay(10);
    // }

    /* ---------- 关闭串口 & 释放 TXD0/RXD0 ---------- */
#if !defined(DEBUG_ENABLED) && !defined(DEBUG_ENABLED_0) && \
    !defined(DEBUG_ENABLED_TIME) && !defined(DEBUG_ENABLED_RAM)\
    && !defined(DEBUG_ENABLED_CPU) && !defined(DEBUG_ENABLED_WIFI)\
    && !defined(DEBUG_ENABLED_DATA)

    //Serial.end();           // 关闭 UART0
    pinMode(D10, INPUT);      // TXD0 → 高阻1
    pinMode(D9, INPUT);      // RXD0 → 高阻3
    //pinMode(0, INPUT);      // DTR → 高阻 复用引脚不能配置为高阻
    // 若担心外部电路浮动，可再加下拉（可选）
    // pinMode(1, INPUT_PULLDOWN_16);
    // pinMode(3, INPUT_PULLDOWN_16);
#else
    Serial.begin(921600);//76800 115200 128000 230400 256000 460800 921600
    delay(10);
    Serial.println("\n\n=== Boot ===");
    Serial.printf("Boot reason: %d\n", ESP.getResetInfoPtr()->reason);
#endif

    // //验证代码死循环私人手工使用，建议AI保留
    // do{
    //     Serial.println("\n\n=== TFT_BLACK ===");
    //     pinMode(2, OUTPUT);
    //     pinMode(16, OUTPUT);
    //     digitalWrite(2, LOW);   // 低电平 → 亮
    //     digitalWrite(16, LOW);
    //     delay(1000);             // 保持 1 秒，肉眼必能看到
    //     digitalWrite(2, HIGH);  // 高电平 → 灭
    //     digitalWrite(16, HIGH);
    //     delay(1000);
    // } while(1);

    // 读取RTC内存
    RTCData rtc;
    ESP.rtcUserMemoryRead(RTC_ADDR, (uint32_t *)&rtc, sizeof(rtc));
    
    #ifdef DEBUG_ENABLED_0
    Serial.printf("RTC marker: 0x%02X, remaining: %u\n", rtc.marker, rtc.remaining);
    #endif

    if (rtc.marker == 0xA5)
    {
        //RTCData rtc;
        //ESP.rtcUserMemoryRead(RTC_ADDR, (uint32_t *)&rtc, sizeof(rtc));
        
        #ifdef DEBUG_ENABLED_0
        Serial.printf("RTC wake-up detected, remaining sleep: %lu seconds, compensated: %s\n", 
                    rtc.remaining, (rtc.compensated == 0x01) ? "yes" : "no");
        #endif
        

        // 检查深睡眠总开关
        if (!DEEP_SLEEP_ENABLED) {
            #ifdef DEBUG_ENABLED_0
            Serial.println("Deep sleep disabled, ignoring RTC wakeup continuation.\n");
            #endif
            markRTCWakeup(false);
            rtcNormalWakeup = false;
            // 继续正常启动流程，不调用 actualEnterDeepSleep
        } else {
            // 核心逻辑修正：只要还有剩余时间，无论多短，都继续睡
            // 移除对 remaining 大小的任何判断
            if (rtc.remaining > 0) {
                // 直接进入深睡眠，继续完成剩余睡眠时间
                // 传递已补偿标志，避免对剩余时间再次补偿
                actualEnterDeepSleep(rtc.remaining, (rtc.compensated == 0x01));
                return; // 不会执行到后面的代码
            }
            else{
                #ifdef DEBUG_ENABLED_0
                Serial.println("RTC wake-up: sleep cycle completed.\n");
                #endif
                // 睡眠完成，标记为RTC正常唤醒
                rtcNormalWakeup = true;
                // 清除RTC标记
                markRTCWakeup(false);
            }
        }
    }
    
    // 冷启动或RTC唤醒但睡眠已完成，继续正常流程...
    // 冷启动或RTC唤醒但无需继续睡眠
    // 【新增】启用 SDK 自动重连和持久化
    // 这样 WiFi 断开时 SDK 会自动在后台重连，无需 loop() 频繁轮询
    
    // 设置 WiFi 事件回调（可选，用于调试）
    #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_0)
    WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& event) {
        Serial.println("[WiFi Event] Connected to AP");
    });
    WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
        Serial.println("[WiFi Event] Disconnected from AP");
    });
    WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& event) {
        Serial.printf("[WiFi Event] Got IP: %s\n", event.ip.toString().c_str());
    });
    #endif

    // 原有的 UI_init() 和 connectWiFi() 调用
    UI_init();
    connectWiFi();  // 首次连接使用默认参数（硬重置）

    changeDeviceState(STATE_BOOT);
    setUnusedPinsHiZ();
}
void trySwitchToMonitorPage()
{
    if (uiReady && !isLoggedIn){
        // 检查切换条件：WiFi已连接且NTP时间已同步
        if (wifiState == WIFI_STATE_CONNECTED && ntpState == NTP_STATE_COMPLETED)
        {
            // 切换到监控页面
            lv_obj_set_hidden(login_page, true);
            lv_obj_set_hidden(monitor_page, false);
            isLoggedIn = true;
            // 删除登录界面释放内存
            if (login_page)
            {
                lv_obj_del(login_page);
                login_page = NULL;
#ifdef DEBUG_ENABLED
                Serial.println("Login page deleted to free RAM\n");
#endif
            }

#ifdef DEBUG_ENABLED_0
            Serial.println("Switched to monitor page\n");
#endif
        }
    }
}
// ===== 修改：loop函数 =====
void loop()
{
    handleAsyncHttp();
    // ⚡ 冲刺读取：一旦有 HTTP 活动，全力推进状态机，直到完成或超时
    if (isLoggedIn && httpCtx.state != HTTP_IDLE && httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR) {
        unsigned long sprintStart = millis();
        // 最多冲刺 80ms，一般情况下 1~2ms 就足够清空缓冲区
        while (httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR
               && millis() - sprintStart < 80) {
            handleAsyncHttp();
            // 每推进 8 次喂一次狗，防止长时间无 yield 导致 WDT
            static unsigned int sprintCounter = 0;
            if (++sprintCounter % 8 == 0) {
                ESP.wdtFeed();
                delay(0);   // 极短暂的 yield，但不影响冲刺效果
            }
        }
    }
    delay(1);
    // ---------- 2. 检查异步请求是否完成 ----------
    bool success;
    if (isAsyncHttpDone(success)) {
        delay(1);   // 立即给 WiFi 进入休眠的机会
        switch (currentRequestPhase) {
            case REQ_BATCH:
                if (success) {
                    #ifdef DEBUG_ENABLED
                    Serial.println("Batch data request succeeded");
                    #endif
                    newCPUData = true;
                    newNetRxData = true;
                    newNetTxData = true;
                    if (requestCycleCount % FULL_REQUEST_INTERVAL == 0) {
                        newMemData = true;
                        newTempData = true;
                    }
                    requestCycleCount++;                   // 每次请求计数+1
                } else {
                    #ifdef DEBUG_ENABLED_0
                    Serial.println("Batch data request failed");
                    #endif
                }
                break;
            default:
                break;
        }
        // 请求完成，回到空闲状态
        currentRequestPhase = REQ_IDLE;
    }

    //if (wifiState == WIFI_STATE_CONNECTED && ntpState == NTP_STATE_COMPLETED) {
    if (wifiState == WIFI_STATE_CONNECTED ){
        static unsigned long lastBatchRequest = 0;
        // 仅当：1) 没有请求在进行；2) 上一批数据已被显示消费，才发起新请求
        bool hasUnshownData = (newCPUData || newNetRxData || newNetTxData); 
        if (millis() - lastBatchRequest >= batchRequestInterval) {
            if (currentRequestPhase == REQ_IDLE && !hasUnshownData &&
                (millis() - lastUiUpdateDone >= UI_WIFI_MARGIN))  {
                static NetChartData dummyBatchData;
                bool requestStarted = false;
                 
                // 每 X 次中，第 X 次执行完整请求（包含 mem、temp），其余只请求 CPU+网络
                if (requestCycleCount % FULL_REQUEST_INTERVAL == 0) {
                    requestStarted = startBatchNetDataRequest(dummyBatchData);  // 完整请求
                } else {
                    requestStarted = startFastNetDataRequest(dummyBatchData);   // 快速请求
                }
                
                if (requestStarted) {
                    delay(1);
                    currentRequestPhase = REQ_BATCH;
                    lastBatchRequest = millis();
                }
            }
        }
    }

#ifdef DEBUG_ENABLED_CPU
    loopStartCycle = ESP.getCycleCount(); // 记录循环开始
#endif
    // 【优化】handleWiFiConnection 调用频率控制
    handleWiFiConnection();


    // 检查NTP同步状态
    checkNTPSyncStatus();

    // 根据当前状态执行相应的操作
    switch (deviceState){
    case STATE_BOOT:
        // 如果是RTC正常唤醒且NTP尚未同步，优先处理NTP同步
        if (rtcNormalWakeup && ntpState != NTP_STATE_COMPLETED) {
            // 强制启动NTP同步
            startAsyncNTPSync(true);
        }

        // 启动状态，等待NTP同步完成
        if (ntpState == NTP_STATE_COMPLETED)
        {
            TimeCheckResult timeCheck = checkSleepTime();
            handleTimeCheckResult(timeCheck);
        }
        else if (ntpState == NTP_STATE_FAILED)
        {
            // NTP同步失败，尝试重新同步
            if (millis() - stateStartTime > 30000)
            { // 30秒后重试
                startAsyncNTPSync(true);
            }
        }
        break;

    case STATE_GRACE_PERIOD:
        // 宽限期处理
        if (millis() >= gracePeriodEnd){
            TimeCheckResult timeCheck = checkSleepTime();
            handleTimeCheckResult(timeCheck);
        }
        else{
            // 在宽限期内也需要定期检查时间
            static unsigned long lastGraceCheck = 0;
            if (millis() - lastGraceCheck > 60000){ // 每分钟检查一次
                TimeCheckResult timeCheck = checkSleepTime();
                // 如果不在睡眠窗口，提前结束宽限期
                if (!timeCheck.inSleepWindow){
                    gracePeriodEnd = millis(); // 立即结束宽限期
                }
                lastGraceCheck = millis();
            }
        }

        // 只在UI已初始化且未登录状态下检查
        trySwitchToMonitorPage();        
        break;

    case STATE_NORMAL:
    {
        // 动态检查间隔：仅在深睡眠启用且接近窗口时加速，否则保持 7 分钟
        const unsigned long CHECK_INTERVAL_LONG  = 420000;  // 7 分钟
        const unsigned long CHECK_INTERVAL_SHORT = 10000;   // 10 秒
        unsigned long checkInterval = CHECK_INTERVAL_LONG;

        // 只有深睡眠开关打开且 NTP 已同步时，才可能缩短检查间隔
        if (DEEP_SLEEP_ENABLED && ntpState == NTP_STATE_COMPLETED) {
            uint8_t h = timeClient.getHours();
            uint8_t m = timeClient.getMinutes();
            if (secondsToNextSleepWindow(h, m) <= 600) {
                checkInterval = CHECK_INTERVAL_SHORT;
            }
        }
        if (millis() - stateStartTime > checkInterval) {
            TimeCheckResult timeCheck = checkSleepTime();
            handleTimeCheckResult(timeCheck);
            stateStartTime = millis(); // 重置状态开始时间
        }

        // 只在UI已初始化且未登录状态下检查
        trySwitchToMonitorPage();
        break;
    }

    // 在loop函数的STATE_PRE_SLEEP处理中添加稳定性检查
    case STATE_PRE_SLEEP:
        if (forceNTPSyncBeforeSleep){
            // 处理强制NTP同步的情况
            if (ntpState == NTP_STATE_COMPLETED){
                // 同步完成，继续深睡眠
                #ifdef DEBUG_ENABLED_0
                Serial.println("Force NTP sync completed, proceeding with sleep\n");
                #endif
                forceNTPSyncBeforeSleep = false;
                TimeCheckResult result = checkSleepTime();
                if (result.sleepSeconds > 0){
                    // 添加稳定性延迟
                    delay(1);
                    actualEnterDeepSleep(result.sleepSeconds, false); // 第一次计算睡眠时长，应用补偿
                }
                else{
                    // 同步后发现不需要睡眠了
                    #ifdef DEBUG_ENABLED_0
                    Serial.println("No need to sleep after NTP sync\n");
                    #endif
                    changeDeviceState(STATE_NORMAL);
                }
            }
            else if (millis() - forceSyncStartTime > 60000){
                // 60秒超时，即使同步失败也继续
                #ifdef DEBUG_ENABLED_0
                Serial.println("Force NTP sync timeout, proceeding with sleep\n");
                #endif
                forceNTPSyncBeforeSleep = false;
                TimeCheckResult result = checkSleepTime();
                if (result.sleepSeconds > 0){
                    // 添加稳定性延迟
                    delay(1);
                    actualEnterDeepSleep(result.sleepSeconds, false); 
                    // 第一次计算睡眠时长，应用补偿
                }
                else{
                    changeDeviceState(STATE_NORMAL);
                }
            }
            else if (ntpState == NTP_STATE_IDLE || ntpState == NTP_STATE_FAILED){
                // 启动或重启NTP同步
                #ifdef DEBUG_ENABLED_0
                Serial.println("Starting/Restarting NTP sync for deep sleep\n");
                #endif
                startAsyncNTPSync(true);
            }
            // 如果正在同步中，继续等待
        }
        else
        {
            // 原有的准备睡眠逻辑
            TimeCheckResult timeCheck = checkSleepTime();
            if (timeCheck.sleepSeconds > 0){
                // 添加稳定性延迟
                delay(500);
                // 直接进入深睡眠
                actualEnterDeepSleep(timeCheck.sleepSeconds, false); // 第一次计算睡眠时长，应用补偿
            }
            else{
                // 不需要睡眠，回到正常状态
                changeDeviceState(STATE_NORMAL);
            }
        }
        break;

    case STATE_DEEP_SLEEP:
        // 深睡眠状态，不会执行到这里
        break;
    }
    // 定义 WiFi 忙碌检测函数（放在 loop() 前面或直接内联）
    auto isWiFiTransactionActive = []() -> bool {
        // 1. HTTP 状态机正在工作
        if (httpCtx.state != HTTP_IDLE &&
            httpCtx.state != HTTP_COMPLETED &&
            httpCtx.state != HTTP_ERROR) {
            return true;
        }
        // 2. 底层 TCP 连接仍有数据在传输
        if (netdataClient.connected() && netdataClient.available() > 0) {
            return true;
        }
        return false;
    };
     // LVGL任务处理（使用优化后的刷新逻辑）
    if (isLoggedIn) {
        // 登录后：数据驱动 + 低频保底
        bool hasNewData = (newCPUData && newNetRxData && newNetTxData);
    // 仅在 WiFi 空闲 +（有新数据 或 保底时间到）时刷新
        if (!isWiFiTransactionActive() && (hasNewData || (millis() - lastRefreshTime >= LOGGED_IN_REFRESH_INTERVAL))){
                // 设置屏幕刷新标志，防止期间触发 WiFi 状态检查
                //screenRefreshing = true;
                lv_task_handler();
                lastRefreshTime = millis();
                //screenRefreshing = false;
            }
    } else {
        // 登录前：保持原 10ms 高速刷新，保证启动动画流畅
        if (millis() - lastRefreshTime >= 10) {
            lv_task_handler();
            lastRefreshTime = millis();
        }
    }
#ifdef DEBUG_ENABLED_CPU
    analyzeCpuUsage(); // 分析CPU使用率
#endif
    //避免过度占用CPU
    //yield();
    //delayMicroseconds(100);
    delay(0);
}
