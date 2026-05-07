// 功能说明
// wifi连接无阻塞,NTP时间同步无阻塞,屏幕显示无阻塞；
// NTP每4小时与NTP服务器同步一次；
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
// #define DEBUG_ENABLED        // 串口查看基本信息刷屏
// #define DEBUG_ENABLED_0    // 串口查看基本信息一般不刷屏
// #define DEBUG_ENABLED_TIME // 串口查看时间相关信息
// #define DEBUG_ENABLED_RAM    // 串口查看Task_cb中内存占用情况
// #define DEBUG_ENABLED_CPU    // 串口查看loop中cpu占用率
// #define DEBUG_ENABLED_DATA   // 串口查看获取NetData的数据
// #define DEBUG_ENABLED_WIFI   // 取消注释以启用WiFi调试信息
// #define DEBUG_ENABLED_POWER   //查看WIFI功率
// #define DEBUG_ENABLED_STATE   // 取消注释以打印状态机切换信息
// #define DEBUG_ENABLED_NTP    // 取消注释以启用 NTP 同步细节日志
// #define DEBUG_ENABLED_DEEPSLEEP    // 取消注释以启用 DEEPSLEEP 同步细节日志

const char *ssid = "AX";           // 连接WiFi名（此处使用AX为示例）
                                   // 请将您需要连接的WiFi名填入引号中
const char *password = "12345678"; // 连接WiFi密码（此处使用12345678为示例）
// NetData服务器配置
#define NETDATA_SERVER_IP "192.168.10.1" // 定义被监控的NetData服务器地址
#define NETDATA_SERVER_PORT 19999        // NetData服务器端口
// 修改数据获取接口的相关用AI搜索相关代码
// 下面是路由器cpu温度接口关键词
#define CHART_NET "net.wan"
#define CHART_CPU "system.cpu"
#define CHART_MEM "mem.available"
#define CHART_TEMP "sensors.temp_thermal_zone0_thermal_thermal_zone0_thermal_zone0"
// 被监控的路由器Ram大小单位MB
#define CHART_MEM_X 1024.0
// 维度过滤数据是各个监控值下一级的关键词，用来确定获取到的数据分别是什么值
// 交换宏定义可以改变数据显示方向
#define DIM_RX "received"
#define DIM_TX "sent"
// #define DIM_TX            "received"
// #define DIM_RX            "sent"
// 注意：维度名必须与 NetData 中实际名称一致，
// 请根据解析函数（ parseBatchArrayResponse）中使用的维度名调整。
// 当前解析中使用的维度为：
//   CPU: DIM_CPU  |  网络: DIM_RX","DIM_TX  |  内存: 含 DIM_MEM |  温度: 含 DIM_TEMP
// 下面字符串包含了这些关键字的常见精确名称，如果与实际不符，请通过串口输出一次完整响应调整。
// 显示不正确可尝试注销parseBatchArrayResponse中的下面代码，然后查询维度数据修复
// + "&dimensions="+DIM_RX+","+DIM_TX+","+DIM_CPU+","+DIM_MEM+","+DIM_TEMP;
// 查看维度名称，可在parseBatchArrayResponse函数中的串口日志方式实时jsonStr；

// 路由器CPU维度数据
#define DIM_CPU "system"
// 路由器Ram维度数据
#define DIM_MEM "avail"
// 路由器CPU温度维度数据
#define DIM_TEMP "temp"

// 路由器中设备hostname
#define ROUTERMONITORPLUS_8266_HOSTNAME "RouterMonitorPlus"
// #define ROUTERMONITORPLUS_8266_HOSTNAME "RouterMonitor"

// 深睡眠总开关： true  启用深睡眠功能， false  完全禁用深睡眠
// #define DEEP_SLEEP_ENABLED true
#define DEEP_SLEEP_ENABLED false
// 深睡眠时间段（24h制,精确到分钟） 定时
constexpr uint8_t SLEEP_START_HOUR = 21; // 开始：21:10
constexpr uint8_t SLEEP_START_MIN = 10;
constexpr uint8_t SLEEP_END_HOUR = 7; // 结束：07:20
constexpr uint8_t SLEEP_END_MIN = 20;
// 深睡眠时间检查精度
const unsigned long CHECK_INTERVAL_LONG = 120000; //3分钟睡眠窗口外
const unsigned long CHECK_INTERVAL_SHORT = 20000; //3分钟睡眠窗口内
// 宽限期时限
constexpr uint32_t POST_POWERON_GRACE_MS = 5 * 60 * 1000UL; // 重新上电(不是Reset)后显示5min监控再进入深睡眠
// ┌─────────────────────────────────────────────────────────────────────────┐
// │    方便随时调整定时补偿比例或关闭补偿,建议2%,8266RTC计时器精度-5%左右,偏快   │
// └─────────────────────────────────────────────────────────────────────────┘
// 具体多少可以打开#define DEBUG_ENABLED_0和正点原子串口调试助手的时间戳功能监控深睡眠一段时间查看
#define RTC_COMPENSATE_PERCENT 2 // 补偿 2 % 由于8266RTC计时器精度不够，所以提前2%的时间醒来校正时间
#define MIN_SLEEP_WINDOW_SEC 300 // 300 // 5 min,小于等于该值就不再睡
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
// ===== 控制全量数据请求的间隔次数 =====
// 每 FULL_REQUEST_INTERVAL 次请求中，第1次获取全部数据，剩余次数仅获取 CPU + 网速
// 默认值 5 表示每隔5秒请求一次内存和温度，其余4秒只请求变化快的指标cpu，网速
#define FULL_REQUEST_INTERVAL 5
// 全局变量 task_cb 完成时刻标记
unsigned long lastUiUpdateDone = 0; // task_cb 完成时刻
// 监控数据更新延时
// 实际数据更新频率受到：是否以显示过，UI是否刷新完成，网络请求状态是否空闲等共同限制
// 实际数据更新频率被 task_cb 的运行频率牢牢锁住
const unsigned long batchRequestInterval = 300; // 批量请求监控数据更新延时
const unsigned long UI_WIFI_MARGIN = 100;       // UI 完成到允许发起请求的最小间隔(ms)

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ESP8266WiFi.h>
#include "NetData.h"
#include <WiFiUdp.h>
#include <TimeLib.h>
WiFiClient netdataClient ;   // 来自 NetData.h 的客户端
IPAddress cachedServerIP;
bool ipCached = false;

// ==== 全局变量 ====
WiFiUDP ntpUDP; // 复用现有 UDP 对象
// ===== 非阻塞 NTP 专用变量 =====
const char *NTP_SERVER = "cn.pool.ntp.org";
const uint16_t NTP_PORT = 123;        // NTP 服务器端口
const uint16_t LOCAL_NTP_PORT = 2390; // 本地监听端口，保持与原生一致
const unsigned long NTP_PACKET_SIZE = 48;
IPAddress ntpServerIP;
bool ntpServerResolved = false;
time_t ntpCurrentEpoch = 0; // 0 表示无效

unsigned long lastNTPSyncMillis = 0;   // 最近一次NTP同步成功的时间戳
unsigned long lastNTPRetryTime = 0;   // NTP上次重试时间，用于控制重试间隔
unsigned long wifiConnectedTime = 0;   // 记录 WiFi 连接成功的毫秒时间戳
static unsigned long wifiAttemptStart = 0;
static int wifiAttempts = 0;
unsigned long waitingRestartTime = 0;   // 超过最大重试次数后开始等待重启的时间

// NTP 状态标志（供主状态机读取）
bool ntpCompleted = false; // 最近一次同步成功
bool ntpFailed = false;    // 最近一次同步失败，需主状态机处理后清零
// ==== NTP同步控制 ====
// lastNTPSyncMillis 同时用于定期同步和强制同步时间基准
const unsigned long NTP_INTERVAL = 4UL * 3600 * 1000;  // 4 小时
// 深睡眠前半小时强制同步
const unsigned long FORCE_SYNC_INTERVAL = 30 * 60 * 1000; //分钟

// 辅助函数：获取带时区的本地时间
long gmtOffsetSec = 8 * 3600; // 东八区
time_t getCurrentEpoch() {
    if (!ntpCompleted) return 0;      // 未同步成功，返回无效时间
    unsigned long now = millis();
    if (now < lastNTPSyncMillis)      // 发生溢出（唤醒后 millis 重置）
        return ntpCurrentEpoch;       // 简单保持最后同步时间
    unsigned long elapsed = now - lastNTPSyncMillis;
    return ntpCurrentEpoch + (elapsed / 1000);
}

int getCurrentHourLocal() {
    time_t localEpoch = getCurrentEpoch() + gmtOffsetSec;
    return (localEpoch % 86400) / 3600;
}

int getCurrentMinuteLocal() {
    time_t localEpoch = getCurrentEpoch() + gmtOffsetSec;
    return (localEpoch % 3600) / 60;
}

// 全局变量
bool rtcNormalWakeup = false; // 标记是否为RTC正常唤醒（睡眠周期完成）

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

//具体可开串口日志，查看8266上面的接受信号；对比路由器的信号自己确定
//我家情况
// RX：[WiFi-PWR]  | setOutputPower(15.5 dBm), RSSI=-39
// RX：[WiFi-PWR]  | setOutputPower(14.5 dBm), RSSI=-38
// RX：[WiFi-PWR]  | setOutputPower(13.5 dBm), RSSI=-38
// RX：[WiFi-PWR]  | setOutputPower(12.5 dBm), RSSI=-38
// RX：[WiFi-PWR]  | setOutputPower(11.5 dBm), RSSI=-37
// RX：[WiFi-PWR]  | setOutputPower(10.5 dBm), RSSI=-35
// RX：[WiFi-PWR]  | setOutputPower(9.5 dBm), RSSI=-41
// RX：[WiFi-PWR]  | setOutputPower(8.5 dBm), RSSI=-37
// RX：[WiFi-PWR]  | setOutputPower(7.5 dBm), RSSI=-39
// RX：[WiFi-PWR]  | setOutputPower(6.5 dBm), RSSI=-36
// RX：[WiFi-PWR]  | setOutputPower(5.5 dBm), RSSI=-36
// RX：[WiFi-PWR]  | setOutputPower(4.5 dBm), RSSI=-34
// RX：[WiFi-PWR]  | setOutputPower(4.0 dBm), RSSI=-35
//8266WIFI功率越低一些，信号可能会好

// ===== 功率偏向：优先降低功率 =====
// 正偏置会让系统在信号良好时主动压低功率，达到省电和减少干扰的目的
// 0 = 无偏向；推荐 0.3~0.8，值越大压低越积极

// ===== 自适应 RSSI 检查频率 =====
// ===== WiFi 功率动态调整（根据 RSSI） =====
static unsigned long lastRSSICheck = 0;
const unsigned long RSSI_CHECK_FAST = 10000;   // 前3分钟快速间隔10秒
const unsigned long RSSI_CHECK_SLOW = 60000;   // 之后慢速间隔60秒
const unsigned long FAST_PHASE = 180000;       // 3分钟
// 伪漫游
#define RSSI_WEAK_THRESHOLD -72 // RSSI低于此值直接重启小电视
int weakRssiCount = 0;
const int WEAK_RSSI_LIMIT = 3; // 连续3次低于阈值才动作

// 在此处列出SD2小电视真正用到的引脚
// ：TFT_BL = 5，TFT_DC = 4，LED = 2，DC=0， RES=2， SCK=14, MOSI=13, 其余引脚区别设置为高阻
static const uint8_t IN_USE_PINS[] = {5, 4, 2, 0, 14, 13}; // ← 按需增删
static const size_t IN_USE_COUNT = sizeof(IN_USE_PINS) / sizeof(IN_USE_PINS[0]);

// === NTP 连续失败计数与自动恢复 ===
uint8_t ntpConsecutiveFailures = 0; // 连续失败次数
const uint8_t NTP_MAX_FAILURES = 5; // 达到此次数后重启系统

// ===== 统一设备状态枚举 =====
enum AppState : uint8_t
{
    APP_INIT,            // 初始状态，等待 WiFi
    APP_WIFI_CONNECTING, // 正在连接 WiFi
    APP_NTP_SYNCING,     // WiFi 已连，正在进行 NTP 同步
    APP_OPERATIONAL,     // 正常运行（WiFi 连、NTP 同步、可获取数据/决策睡眠）
    APP_GRACE_PERIOD,    // 断电后宽限期（WiFi 连、NTP 同步）
    APP_PRE_SLEEP,       // 准备深睡眠（可能强制 NTP 同步）
    APP_DEEP_SLEEP,      // 深睡眠（代码不会运行到这里，仅标记）
    APP_FAULT            // 致命错误，等待重启
};
AppState appState = APP_INIT;

// ===== 替换原有分散的状态变量 =====
unsigned long stateStartTime = 0; // 状态开始时间
unsigned long gracePeriodEnd = 0; // 宽限期结束时间

bool isLoggedIn = false; // 登录状态标志
unsigned long lastRefreshTime = 0;
const unsigned long LOGGED_IN_REFRESH_INTERVAL = 499; // 登录后屏幕刷新间隔(ms)待机时间 不建议修改

// ===== 新增：时间检查结果结构体 =====
struct TimeCheckResult
{
    bool inSleepWindow;    // 是否在睡眠窗口内
    uint32_t sleepSeconds; // 需要睡眠的秒数(0表示不需要睡眠)
    uint8_t currentHour;   // 当前小时
    uint8_t currentMinute; // 当前分钟
};
const unsigned long WIFI_POST_CONNECT_DELAY_MS = 200; // 200毫秒连接后延迟（稳定等待）
 // ===== WiFi 重连间隔（毫秒），区分复位类型 =====
#define WIFI_RECONNECT_HARD_INTERVAL  20000  // WIFI硬复位重连接超时
#define WIFI_RECONNECT_SOFT_INTERVAL  5000  // WIFI软复位重连接超时推荐 4000~5000ms
const int MAX_WIFI_RECONNECT_ATTEMPTS = 10;          // 最大重试次数

// 修改RTC内存读取方式
struct RTCData
{
    uint8_t marker;      // 唤醒标记 (0xA5)
    uint8_t compensated; // 是否已应用补偿 (0x01=已应用)
    uint8_t padding[2];  // 填充字节，确保4字节对齐
    uint32_t remaining;  // 剩余睡眠时间
} __attribute__((packed, aligned(4)));

// 修改RTC内存地址常量
constexpr uint32_t RTC_ADDR = 64; // 使用64字节偏移，避免与系统冲突
                                  // ESP8266 RTC user memory 起始地址（4字节对齐）

// 在全局变量区域添加
bool forceNTPSyncBeforeSleep = false;
unsigned long forceSyncStartTime = 0;

// 屏幕状态控制
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

static lv_obj_t *wifi_status_led = NULL; // WiFi状态指示灯

// ===== 异步数据请求调度器 =====
enum DataRequestPhase
{
    REQ_IDLE, // 空闲，未发起请求
    REQ_BATCH // 批量请求中（新增）
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
lv_coord_t upload_series[10] = {0};
lv_coord_t download_series[10] = {0};

// ===== 前置声明 =====
// 函数声明
void UI_init();
void setBrightness(int value);
static bool startAsyncNTPSync(bool force);
bool isTimeInSleepWindow(uint8_t hour, uint8_t minute);
void enterDeepSleep();
void checkNTP();
uint32_t secondsToEndOfSleepWindow(uint8_t hour, uint8_t minute);
void actualEnterDeepSleep(uint32_t, bool alreadyCompensated);
void setAppState(AppState newState);
TimeCheckResult checkSleepTime();
void setDisplayState(bool enable);
void handleWiFiHardware();
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
    time_t epochTime = ntpCurrentEpoch + gmtOffsetSec;

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
void analyzeCpuUsage()
{
    loopEndCycle = ESP.getCycleCount();
    loopCycleCount += (loopEndCycle - loopStartCycle);
    loopCounter++;

    unsigned long currentTime = millis();
    if (currentTime - lastCpuReportTime >= CPU_REPORT_INTERVAL)
    {
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

// ===== 设置WiFi发射功率 =====
// power_dBm: 0.0 ~ 20.5 dBm
void setWiFiTxPower(float power_dBm)
{
    if (power_dBm < 0.0)
        power_dBm = 0.0;
    if (power_dBm > 20.5)
        power_dBm = 20.5;

    WiFi.setOutputPower(power_dBm);

#if defined(DEBUG_ENABLED) || defined(DEBUG_ENABLED_POWER)
    static float lastLoggedPower = -1.0;
    if (fabs(lastLoggedPower - power_dBm) > 0.1)
    { // 避免重复打印相同值
        if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi-PWR] TTL=%lu | setOutputPower(%.1f dBm), RSSI=%d\n", 
                  millis(), power_dBm, WiFi.RSSI());
        } else {
        Serial.printf("[WiFi-PWR] TTL=%lu | setOutputPower(%.1f dBm), RSSI=n/a\n", 
                millis(), power_dBm);
        }
        lastLoggedPower = power_dBm;
    }
#endif
}

// ===== 迟滞控制器专用变量 =====
static float currentPowerHyst = WIFI_TX_POWER_DBM;
static unsigned long lastHystAdjust = 0;
const unsigned long HYST_MIN_INTERVAL = 10000; // 最小调整间隔 10 秒，防止过快振荡


void dynamicAdjustTxPower(int currentRSSI) {
    if (!ENABLE_DYNAMIC_TX_POWER) return;
    if (currentRSSI >= 0) return;

    // ---------- 1. 滑动平均 (窗口7，去极值) ----------
    static int rssiBuffer[7] = {0};
    static uint8_t rssiBufIndex = 0;
    static uint8_t rssiBufCount = 0;

    rssiBuffer[rssiBufIndex] = currentRSSI;
    rssiBufIndex = (rssiBufIndex + 1) % 7;
    if (rssiBufCount < 7) rssiBufCount++;

    float avgRSSI = 0.0f;
    if (rssiBufCount >= 3) {
        int sorted[7];
        for (uint8_t i = 0; i < rssiBufCount; i++) sorted[i] = rssiBuffer[i];
        // 排序
        for (uint8_t i = 0; i < rssiBufCount - 1; i++)
            for (uint8_t j = i + 1; j < rssiBufCount; j++)
                if (sorted[i] > sorted[j]) {
                    int tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
                }
        float sum = 0;
        for (uint8_t i = 1; i < rssiBufCount - 1; i++) sum += sorted[i];
        avgRSSI = sum / (rssiBufCount - 2);
    } else {
        float sum = 0;
        for (uint8_t i = 0; i < rssiBufCount; i++) sum += rssiBuffer[i];
        avgRSSI = sum / rssiBufCount;
    }

    // ---------- 2. 迟滞控制 ----------
    const float RSSI_LOW  = -68.0;  // 低于此值需提升功率
    const float RSSI_HIGH = -55.0;  // 高于此值可降低功率
    const float POWER_UP_STEP   = 1.5;   // 单次升功率步长
    const float POWER_DOWN_STEP = 1.0;   // 单次降功率步长

    float newPower = currentPowerHyst;

    if (avgRSSI < RSSI_LOW) {
        newPower = currentPowerHyst + POWER_UP_STEP;
    } else if (avgRSSI > RSSI_HIGH) {
        newPower = currentPowerHyst - POWER_DOWN_STEP;
    } else {
        // 窗口内，不做任何调整
        return;
    }

    // 钳位
    if (newPower < 0.0f)  newPower = 0.0f;
    if (newPower > 20.5f) newPower = 20.5f;

    // 最小调整间隔，防止过快振荡
    if (millis() - lastHystAdjust < HYST_MIN_INTERVAL) return;

    // 若无变化则跳过
    if (fabs(newPower - currentPowerHyst) < 0.1f) return;

    // ---------- 3. 应用新功率 ----------
    setWiFiTxPower(newPower);
    currentPowerHyst = newPower;
    lastHystAdjust = millis();

    // #ifdef DEBUG_ENABLED_POWER
    // Serial.printf("[HYST] avgRSSI=%.1f -> newPower=%.1f dBm (raw RSSI=%d)\n",
    //               avgRSSI, newPower, currentRSSI);
    // #endif
}

void resetDynamicPowerState() {
    currentPowerHyst = WIFI_TX_POWER_DBM;
    lastHystAdjust = 0;
    setWiFiTxPower(WIFI_TX_POWER_DBM);
#if defined(DEBUG_ENABLED) || defined(DEBUG_ENABLED_POWER)
    Serial.printf("[WiFi-PWR] TTL=%lu | Hyst state reset to default %.1f dBm\n", millis(), WIFI_TX_POWER_DBM);
#endif
}

// ===== 状态转换函数 =====
void setAppState(AppState newState)
{
    // 仅在状态真正发生变化时才打印
    if (newState != appState) {
#ifdef DEBUG_ENABLED_STATE
        AppState oldState = appState;       // 保存旧状态
#endif
        appState = newState;
        stateStartTime = millis();
#ifdef DEBUG_ENABLED_STATE
        static const char* stateNames[] = {
            "0:APP_INIT",
            "1:APP_WIFI_CONNECTING",
            "2:APP_NTP_SYNCING",
            "3:APP_OPERATIONAL",
            "4:APP_GRACE_PERIOD",
            "5:APP_PRE_SLEEP",
            "6:APP_DEEP_SLEEP",
            "7:APP_FAULT"
        };
        Serial.printf("[STATE] %s -> %s (TTL=%lu)\n",
                      stateNames[oldState],
                      stateNames[newState],
                      millis());
#endif
    }
}

// ===== 统一时间检查函数 =====
TimeCheckResult checkSleepTime()
{
    TimeCheckResult result = {false, 0, 0, 0};

    // 只有NTP同步完成才能进行时间检查
    if (!ntpCompleted)
    {
#ifdef DEBUG_ENABLED_0
        Serial.println("Cannot check sleep time: NTP not synchronized\n");
#endif
        return result;
    }

    time_t epoch = getCurrentEpoch();
    if (epoch == 0) return result;   // 时间无效

    result.currentHour = getCurrentHourLocal();
    result.currentMinute = getCurrentMinuteLocal();
    result.inSleepWindow = isTimeInSleepWindow(result.currentHour, result.currentMinute);
    if (result.inSleepWindow)
        result.sleepSeconds = secondsToEndOfSleepWindow(result.currentHour, result.currentMinute);
#ifdef DEBUG_ENABLED_DEEPSLEEP
    static unsigned long lastLogTime1 = 0;
    if (millis() - lastLogTime1 > 60000) {  // 每60秒最多打印一次
    Serial.printf("Time check: %02d:%02d, In window: %s, Sleep seconds: %lu\n",
                  result.currentHour, result.currentMinute,
                  result.inSleepWindow ? "Yes" : "No", (unsigned long)result.sleepSeconds);
    lastLogTime1 = millis();
    }
#endif

    return result;
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

    if (enable) {
        // 确保UI已初始化
        if (!uiReady) {
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
    else {
        // 只有UI准备好了，才需要执行反初始化流程
        // 关闭LVGL和显示
        lv_deinit();
        delay(1);
        tft.fillScreen(TFT_BLACK);
        // 额外发送关闭命令确保屏幕完全关闭
        tft.writecommand(TFT_SLPIN); // 进入睡眠模式
        delay(1);
        tft.writecommand(TFT_DISPOFF);
        delay(1);
        setBrightness(SCREEN_BRIGHTNESS_OFF);
        pinMode(TFT_BL, INPUT); // 设置为高阻态，减少功耗

        uiReady = false; // 标记UI需要重新初始化
#ifdef DEBUG_ENABLED_0
        Serial.println("Display turned OFF\n");
#endif
    }
}

// 文件作用域，checkNTP 专用
static enum { NTP_IDLE, NTP_WAIT_RESP } ntpStep = NTP_IDLE;
static unsigned long ntpStepStart = 0;  // NTP同步开始时间

// 尝试发起 NTP 同步（force = true 强制忽略间隔）
// 返回 true 表示已发起或正在进行中；false 表示无需或无法发起
bool tryStartNTPSync(bool force) {
    if (ntpStep != NTP_IDLE) {
#ifdef DEBUG_ENABLED_NTP
        Serial.println("[NTP] Busy (already in progress)");
#endif
        return true;   // 已有进行中
    }

    if (!force && ntpCompleted && (millis() - lastNTPSyncMillis < NTP_INTERVAL)) {
#ifdef DEBUG_ENABLED_NTP
        Serial.println("[NTP] Skip – already synced within interval");
#endif
        return false;
    }

    if (ntpConsecutiveFailures >= NTP_MAX_FAILURES) {
#ifdef DEBUG_ENABLED_NTP
        Serial.printf("[NTP] ABORT – failures %u >= max %u\n",
                      ntpConsecutiveFailures, NTP_MAX_FAILURES);
#endif
        return false;
    }

    if (!force && (millis() - lastNTPRetryTime < 3000)) {
#ifdef DEBUG_ENABLED_NTP
        Serial.println("[NTP] Hold – retry interval not reached");
#endif
        return false;
    }

    lastNTPRetryTime = millis();
#ifdef DEBUG_ENABLED_NTP
    Serial.printf("[NTP] Initiating NTP sync (force=%d)\n", force);
#endif
    return startAsyncNTPSync(force);
}

// ===== 异步NTP非阻塞发送 =====
static bool startAsyncNTPSync(bool force = false)
{
    if (WiFi.status() != WL_CONNECTED) return false;

    if (!force && ntpCompleted && (millis() - lastNTPSyncMillis < NTP_INTERVAL)) {
#ifdef DEBUG_ENABLED_NTP
        Serial.println("[NTP] Already synced, skip (interval not reached)");
#endif
        return true;
    }

    ntpCompleted = false;
    ntpFailed = false;

    // 解析服务器 IP
    if (!ntpServerResolved || force) {
#ifdef DEBUG_ENABLED_NTP
        Serial.print("[NTP] Resolving NTP server...");
#endif
        if (WiFi.hostByName(NTP_SERVER, ntpServerIP) != 1) {
#ifdef DEBUG_ENABLED_NTP
            Serial.println(" FAILED");
#endif
            ntpFailed = true;
            ntpConsecutiveFailures++;
            return false;
        }
#ifdef DEBUG_ENABLED_NTP
        Serial.print(" OK (");
        Serial.print(ntpServerIP);
        Serial.println(")");
#endif
        ntpServerResolved = true;
    }

    // 清理并绑定 UDP 端口（每次重新 begin）
    ntpUDP.stop();
#ifdef DEBUG_ENABLED_NTP
    Serial.print("[NTP] Binding UDP port...");
#endif
    if (ntpUDP.begin(LOCAL_NTP_PORT) == 0) {
#ifdef DEBUG_ENABLED_NTP
        Serial.println(" FAILED (port busy?)");
#endif
        ntpFailed = true;
        ntpConsecutiveFailures++;
        return false;
    }
#ifdef DEBUG_ENABLED_NTP
    Serial.print(" OK (port ");
    Serial.print(LOCAL_NTP_PORT == 0 ? ntpUDP.localPort() : LOCAL_NTP_PORT);
    Serial.println(")");
#endif

    // 构造并发送请求
    uint8_t packet[NTP_PACKET_SIZE] = {0};
    packet[0] = 0b11100011;
    packet[1] = 0;
    packet[2] = 6;
    packet[3] = 0xEC;

    ntpUDP.beginPacket(ntpServerIP, NTP_PORT);
    ntpUDP.write(packet, NTP_PACKET_SIZE);
    ntpUDP.endPacket();

#ifdef DEBUG_ENABLED_NTP
    Serial.println("[NTP] Request sent, waiting for reply...");
#endif

    ntpStep = NTP_WAIT_RESP;
    ntpStepStart = millis();
    return true;
}

// ===== NTP非阻塞轮询 =====
void checkNTP()
{
    const unsigned long TIMEOUT = 10000; // NTP同步超时时间(10秒)

    switch (ntpStep)
    {
    case NTP_IDLE:
        break;

    case NTP_WAIT_RESP:
    {
        if (millis() - ntpStepStart > TIMEOUT)
        {
#ifdef DEBUG_ENABLED_NTP
            Serial.println("[NTP] Response timeout!");
#endif
            ntpUDP.flush();
            ntpUDP.stop();
            ntpFailed = true;
            ntpConsecutiveFailures++;
            ntpStep = NTP_IDLE;
            return;
        }

        int packetSize = ntpUDP.parsePacket();
        if ((unsigned int)packetSize >= NTP_PACKET_SIZE)
        {
            uint8_t packet[NTP_PACKET_SIZE];
            ntpUDP.read(packet, NTP_PACKET_SIZE);

            uint32_t secsSince1900 = 0;
            for (int i = 0; i < 4; i++)
                secsSince1900 = (secsSince1900 << 8) | packet[40 + i];
            const uint32_t seventyYears = 2208988800UL;
            time_t epoch = secsSince1900 - seventyYears;

            if (epoch > 1577836800) //2020‑01‑01 00:00:00 UTC
            {
                ntpCurrentEpoch = epoch;
                ntpCompleted = true;
                lastNTPSyncMillis = millis();
                ntpConsecutiveFailures = 0;
#ifdef DEBUG_ENABLED_NTP
                Serial.printf("[NTP] Sync OK! Unix time: %lu\n", (unsigned long)epoch);
#endif
            }
            else
            {
                ntpUDP.flush();
                ntpFailed = true;
                ntpConsecutiveFailures++;
#ifdef DEBUG_ENABLED_NTP
                Serial.printf("[NTP] Invalid epoch received: %lu\n", (unsigned long)epoch);
#endif
            }

            ntpUDP.stop();
            ntpServerResolved = false;   // 下次重新解析
            ntpStep = NTP_IDLE;
        }
        break;
    }
    }
}

// 屏幕亮度设置，value [0, 256] 越小越亮,越大越暗
void setBrightness(int value)
{
    pinMode(TFT_BL, INPUT);
    analogWrite(TFT_BL, value);
    pinMode(TFT_BL, OUTPUT);
}

// ==== 新增函数：计算到睡眠窗口结束的秒数 ====
uint32_t secondsToEndOfSleepWindow(uint8_t h, uint8_t m)
{
    uint16_t now = h * 60 + m;
    uint16_t start = SLEEP_START_HOUR * 60 + SLEEP_START_MIN;
    uint16_t end = SLEEP_END_HOUR * 60 + SLEEP_END_MIN;

    // 处理跨午夜的情况
    if (start > end)
    {
        // 睡眠时间跨午夜
        if (now >= start)
        {
            // 当前时间在开始时间之后（晚上）
            return ((24 * 60 - now) + end) * 60UL;
        }
        else if (now < end)
        {
            // 当前时间在结束时间之前（早上）
            return (end - now) * 60UL;
        }
    }
    else
    {
        // 睡眠时间不跨午夜
        if (now >= start && now < end)
        {
            return (end - now) * 60UL;
        }
    }

    return 0; // 不在睡眠窗口内
}

// 计算距离下一次睡眠窗口开始的秒数（0 表示当前已在窗口内）
uint32_t secondsToNextSleepWindow(uint8_t hour, uint8_t minute)
{
    uint16_t now = hour * 60 + minute;
    uint16_t start = SLEEP_START_HOUR * 60 + SLEEP_START_MIN;
    uint16_t end = SLEEP_END_HOUR * 60 + SLEEP_END_MIN;

    if (start <= end)
    { // 不跨午夜
        if (now >= start && now < end)
            return 0;
        if (now < start)
            return (start - now) * 60UL;
        return ((24 * 60 - now) + start) * 60UL;
    }
    else
    { // 跨午夜
        if (now >= start || now < end)
            return 0;
        return (start - now) * 60UL;
    }
}

// ---------------- Deep-Sleep Helper -----------------
bool isTimeInSleepWindow(uint8_t h, uint8_t m)
{
    uint16_t now = h * 60 + m;
    uint16_t start = SLEEP_START_HOUR * 60 + SLEEP_START_MIN;
    uint16_t end = SLEEP_END_HOUR * 60 + SLEEP_END_MIN;
#ifdef DEBUG_ENABLED_DEEPSLEEP
    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime > 60000) {  // 每60秒最多打印一次
        Serial.printf("isTimeInSleepWindow: now=%u start=%u end=%u\n", now, start, end);
        lastLogTime = millis();
    }
#endif
    // 处理跨午夜的时间段（如 23:30–06:30）
    if (start <= end)// 不跨天
        return (now >= start && now < end);
    else    // 跨天（如 23:00~07:00）
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

/*
 * 进入深睡眠（支持超长时间自动分段）
 *
 * 由于 ESP8266 单次深睡眠最长约 71 分钟，本函数会自动将超长睡眠切分为多段。
 * 每段醒来后，通过 RTC user memory 存储剩余时间并立即续睡，直到总时间耗尽。
 * 补偿比例（RTC_COMPENSATE_PERCENT）仅在第一次睡眠时应用，避免重复补偿。
 *
 * 参数：
 *   seconds             - 总睡眠秒数（未补偿）
 *   alreadyCompensated  - true 表示 seconds 已是补偿后的值，不再补偿（用于续睡）
 */
void actualEnterDeepSleep(uint32_t seconds, bool alreadyCompensated = false)
{
    // 总开关判断：若禁用深睡眠，则直接退出
    if (!DEEP_SLEEP_ENABLED)
    {
#ifdef DEBUG_ENABLED_0
        Serial.println("Deep sleep disabled by global switch, aborting.\n");
#endif
        markRTCWakeup(false);
        setAppState(APP_OPERATIONAL); // 放弃睡眠则回到正常状态
        return;
    }
    // 健壮性检查：如果秒数为0，则直接返回，不进行睡眠
    if (seconds == 0)
    {
#ifdef DEBUG_ENABLED_0
        Serial.println("actualEnterDeepSleep called with 0 seconds, aborting.\n");
#endif
        markRTCWakeup(false);
        setAppState(APP_OPERATIONAL); // 放弃睡眠则回到正常状态
        return;
    }

    // 检查睡眠时间是否太短
    if (seconds <= MIN_SLEEP_WINDOW_SEC)
    {
#ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep window too small: %lu s, skip deep-sleep\n", (unsigned long)seconds);
#endif
        markRTCWakeup(false);
        setAppState(APP_OPERATIONAL); // 放弃睡眠则回到正常状态
        return;
    }

#ifdef DEBUG_ENABLED_0
    Serial.printf("Actual deep sleep called with %u seconds, Compensated: %s\n",
                  seconds, alreadyCompensated ? "yes" : "no");
#endif

    // 在actualEnterDeepSleep中，补偿计算需要更谨慎
    // 1. 对总睡眠时长进行一次性补偿，补偿系数在定义区可调（默认2%）
    // 补偿只发生在“用户首次请求睡眠”时；RTC唤醒续睡时 alreadyCompensated 为 true，不再补偿。
    uint32_t compensatedsleepSec = seconds;
    if (!alreadyCompensated)
    {
        // 只在总时长足够大时补偿，避免结果为0
        if (seconds > 100)
        { // 只在足够大的值时应用补偿
            compensatedsleepSec = seconds * (100 - RTC_COMPENSATE_PERCENT) / 100;
            // 确保补偿后至少还有1秒
            if (compensatedsleepSec == 0)
                compensatedsleepSec = 1;
        }
    }

    // 检查补偿后的睡眠时间是否足够
    if (compensatedsleepSec <= MIN_SLEEP_WINDOW_SEC)
    {
#ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep window too small after compensate: %lu s, skip deep-sleep\n", (unsigned long)compensatedsleepSec);
#endif
        markRTCWakeup(false);
        setAppState(APP_OPERATIONAL); // 放弃睡眠则回到正常状态
        return;
    }

    // 2. 分割睡眠：单次 RTC 最大约 4260 秒（71分钟）
    //    若补偿后总时长超过此值，则分多段执行，当前段睡 MAX_SLEEP_SEC，剩余存 RTC 供续睡
    // 定义单次最大睡眠时间（ESP8266 RTC 限制，约71分钟）
    const uint32_t MAX_SLEEP_SEC = 4260; // 71分钟 * 60秒 = 4260秒

    uint32_t actualSleep = compensatedsleepSec;
    uint32_t remaining = 0;

    // 核心逻辑修正：如果补偿后的时间超过硬件限制，则必须分割
    // 不再检查剩余时间是否小于 MIN_SLEEP_WINDOW_SEC
    if (compensatedsleepSec > MAX_SLEEP_SEC)
    {
        actualSleep = MAX_SLEEP_SEC;
        remaining = compensatedsleepSec - MAX_SLEEP_SEC;
// 注意：remaining 可能仍 > MAX_SLEEP_SEC，但没关系，下次唤醒后 setup() 会再次读取并继续分割
#ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep duration exceeds single sleep limit, splitting. First segment: %lu sec, (unsigned long)remaining: %lu sec\n", (unsigned long)actualSleep, (unsigned long)remaining);
#endif
    }
    // 否则，不需要分割，一次性睡完
    else
    {
        actualSleep = compensatedsleepSec;
        remaining = 0;
#ifdef DEBUG_ENABLED_0
        Serial.printf("Sleep duration within limit. Single sleep: %lu sec\n", (unsigned long)actualSleep);
#endif
    }

    /* 注意：完全移除原来的“如果剩余时间太小则跳过”的检查块 */

#ifdef DEBUG_ENABLED_0
Serial.printf("Original sleep: %lu sec, compensatedsleepSec: %lu sec\n", (unsigned long)seconds, (unsigned long)compensatedsleepSec);
Serial.printf("Actual sleep: %lu sec, remaining: %lu sec\n", (unsigned long)actualSleep, (unsigned long)remaining);
#endif

    // 存储唤醒标记和剩余睡眠时间
    // 3. 将“分段续睡”的信息写入 RTC user memory
    RTCData rtcData;
    rtcData.marker = 0xA5;                           // 标记为“深睡眠唤醒状态”
    rtcData.compensated = compensatedsleepSec ? 0x01 : 0x00; // 续睡时传入 true，保持补偿标志
    rtcData.padding[0] = 0;
    rtcData.padding[1] = 0;
    rtcData.remaining = remaining; // 非 0 表示下次唤醒需要继续睡
    // 注意：若 remaining == 0，则 marker 仍为 0xA5 但系统启动后会识别为“深睡眠完成”

    ESP.rtcUserMemoryWrite(RTC_ADDR, (uint32_t *)&rtcData, sizeof(rtcData));
    delay(1);

    // 增强验证：检查所有关键字段
    RTCData verifyData;
    ESP.rtcUserMemoryRead(RTC_ADDR, (uint32_t *)&verifyData, sizeof(verifyData));

    bool writeOK = (verifyData.marker == 0xA5) &&
                   (verifyData.compensated == rtcData.compensated) && // 验证补偿标志
                   (verifyData.remaining == remaining);

    if (!writeOK)
    {
#ifdef DEBUG_ENABLED_0
        Serial.println("RTC memory write verification failed!\n");
        Serial.printf("Expected: marker=0xA5, (unsigned long)compensatedsleepSec=0x%02X, (unsigned long)remaining=%lu\n",
                      rtcData.compensated, (unsigned long)remaining);
        Serial.printf("Actual:   marker=0x%02X, (unsigned long)compensatedsleepSec=0x%02X, (unsigned long)remaining=%lu\n",
                      verifyData.marker, verifyData.compensated, (unsigned long)verifyData.remaining);
#endif
        markRTCWakeup(false);
        setAppState(APP_OPERATIONAL); // 放弃睡眠则回到正常状态
        return;
    }

    // 仅在确认必须进入深睡眠后，才关闭显示和WiFi
    // 关闭显示
    setDisplayState(false);

    // 优化后：单次断开 + 强制射频关闭
    closeNetdataConnection(); // 关闭 NetData 连接
    WiFi.disconnect(true);    // 清除连接状态
    WiFi.mode(WIFI_OFF);      // 关闭 WiFi 模式
    delay(1);
    WiFi.forceSleepBegin(); // 强制射频进入睡眠（比 mode OFF 更省电）
    delay(1);               // 确保射频完全关闭

    // 更新WiFi状态
    // WiFi 将在下次 setup() 时重新初始化，此处无需设置 appState

#ifdef DEBUG_ENABLED_0
    Serial.printf("Enter deep-sleep for %lu seconds\n", (unsigned long)actualSleep);
    Serial.flush();
#endif

    // 增加稳定性措施
    delay(1);         // 确保所有串口输出完成
    ESP.wdtDisable(); // 禁用看门狗
    yield();          // 处理 pending 事件

// 进入深睡眠前最后检查
#ifdef DEBUG_ENABLED_0
    Serial.printf("Entering deep sleep for %lu seconds\n", (unsigned long)actualSleep);
    Serial.flush();
    // 增加稳定性延迟
    delay(1);
#endif
    // 直接调用深睡眠函数（无法检查返回值）
    ESP.deepSleep(actualSleep * 1000000ULL, WAKE_RF_DEFAULT);

// 备用方案：软件复位
#ifdef DEBUG_ENABLED_0
    Serial.println("Deep sleep failed, performing software reset\n");
    Serial.flush();
#endif
    ESP.restart();
}

// 判断是否需要强制NTP同步（距上次同步超过 FORCE_SYNC_INTERVAL）
static inline bool shouldForceNtpSync() {
    return (millis() - lastNTPSyncMillis > FORCE_SYNC_INTERVAL);
}

// 睡眠入口：自动处理强制NTP同步需求
static void prepareSleepOrForceSync() {
    if (shouldForceNtpSync() && ntpCompleted) {
        forceNTPSyncBeforeSleep = true;
        forceSyncStartTime = millis();
        tryStartNTPSync(true);
        setAppState(APP_PRE_SLEEP);
    } else {
        enterDeepSleep();
    }
}

// 修改后 - 只做决策
void enterDeepSleep() {
    if (!DEEP_SLEEP_ENABLED) {
        setAppState(APP_OPERATIONAL);
        return;
    }
    // 如果已经在等待强制同步，不再重复发起
    if (forceNTPSyncBeforeSleep) {
        // 已经在 PRE_SLEEP 状态等待，直接返回，让状态机处理
        return;
    }
    if (millis() - lastNTPSyncMillis > FORCE_SYNC_INTERVAL) {
#ifdef DEBUG_ENABLED_0
        Serial.println("Scheduling force NTP sync before deep sleep\n");
#endif
        forceNTPSyncBeforeSleep = true;
        forceSyncStartTime = millis();
        setAppState(APP_PRE_SLEEP);
        tryStartNTPSync(true);   // 立即发起同步
        return;
    }
    TimeCheckResult tc = checkSleepTime();
    if (tc.sleepSeconds > 0) {
        actualEnterDeepSleep(tc.sleepSeconds, false);
    } else {
        setAppState(APP_OPERATIONAL);
    }
}

// 页面初始化
void setupPages()
{
    // setBrightness(SCREEN_BRIGHTNESS_NORMAL); // 设置屏幕亮度为正常值
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

bool connectWiFi(bool forceFullReset) {
    // 若已连接直接返回 true
    if (WiFi.status() == WL_CONNECTED) return true;

    // forceFullReset：true → 强制清除存储的凭据，重新写入 SSID/PW
    if (forceFullReset) {
        WiFi.disconnect(true);
        delay(1);
        WiFi.mode(WIFI_OFF);
        delay(1);
        WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_MODEM_SLEEP);
        setWiFiTxPower(WIFI_TX_POWER_DBM);
        #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_POWER)
        Serial.printf("[WiFi-INIT] TTL=%lu | Hard reset, initial power=%.1f dBm\n", millis(), WIFI_TX_POWER_DBM);
        #endif
        WiFi.hostname(ROUTERMONITORPLUS_8266_HOSTNAME);
        WiFi.begin(ssid, password);          // 填入宏定义的 SSID/PW
    } else {
        // 软复位：仅断开当前连接，使用 SDK 内部保存的凭据快速重连
        WiFi.disconnect(false);
        delay(1);
        WiFi.begin();                        // 无参数，复用上次成功凭据
        delay(1);
        WiFi.hostname(ROUTERMONITORPLUS_8266_HOSTNAME);
        WiFi.setSleepMode(WIFI_MODEM_SLEEP);
        setWiFiTxPower(WIFI_TX_POWER_DBM);
        #if defined(DEBUG_ENABLED_WIFI) || defined(DEBUG_ENABLED_POWER)
        Serial.printf("[WiFi-INIT] TTL=%lu | Soft reset, keep current power\n", millis());
        #endif
    }
    return false; // 连接是异步的，此时未完成
}

void handleWiFiHardware() {
    static unsigned long lastCheck = 0;
    // 检查间隔 2 秒
    if (millis() - lastCheck < 2000) return;
    lastCheck = millis();

    // 睡眠、准睡眠状态不处理 WiFi
    if (appState == APP_DEEP_SLEEP) return;

    // 仅当 WiFi 真的断开，且当前状态在操作中（高于 WIFI_CONNECTING）时，退回到初始重连
    if (WiFi.status() != WL_CONNECTED && appState > APP_WIFI_CONNECTING && appState != APP_FAULT) {
        setAppState(APP_WIFI_CONNECTING);
#ifdef DEBUG_ENABLED_WIFI
        Serial.printf("[WiFi-HW] Disconnected while in state %d, forcing back to 1:APP_WIFI_CONNECTING\n", appState);
#endif
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
#ifdef DEBUG_ENABLED_RAM
    uint32_t task_start = millis();
    static uint32_t last_time = 0;
#endif

     // ---- 统一的 WiFi 状态 + IP 显示（基于真实 WiFi 状态） ----
    if (ip_label && wifi_status_led)
    {
        static lv_color_t lastLedColor = LV_COLOR_BLACK; // 任意不可能匹配的颜色
        static String lastDisplayedText;

        lv_color_t led_color;
        String currentText;

        if (WiFi.status() == WL_CONNECTED) {
            // WiFi 硬件层已连接，显示绿色 + 当前 IP
            led_color = LV_COLOR_GREEN;
            currentText = WiFi.localIP().toString();
        }
        else if (appState == APP_WIFI_CONNECTING) {
            // 状态机正在尝试连接，显示黄色
            led_color = LV_COLOR_YELLOW;
            currentText = "Connecting...";
        }
        else {
            // 其他状态（初始化、未连接、故障等）显示红色
            led_color = LV_COLOR_RED;
            currentText = "WiFi Disconnected";
        }

        // 仅当颜色或文本真正变化时才刷新控件
        if (led_color.full != lastLedColor.full || currentText != lastDisplayedText) {
            lv_obj_set_style_local_bg_color(wifi_status_led, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, led_color);
            lv_label_set_text(ip_label, currentText.c_str());
            lastLedColor = led_color;
            lastDisplayedText = currentText;
        }
#ifdef DEBUG_ENABLED
Serial.printf("[UI] WiFi state updated: %d, Text: %s\n", appState, currentText.c_str());
#endif
    }

    // ===== 修改：只在WiFi连接成功时获取网络数据 =====
    // 降低部分数据获取频率
    if (appState == APP_OPERATIONAL || appState == APP_GRACE_PERIOD || appState ==APP_PRE_SLEEP) // 时间已同步，WiFi 已连
    {
        if (newCPUData)
        {
            lv_bar_set_value(cpu_bar, cpu_usage, LV_ANIM_OFF);
            lv_label_set_text_fmt(cpu_value_label, "%2.1f%%", cpu_usage);
            newCPUData = false;
        }
        if (newMemData)
        {
            lv_bar_set_value(mem_bar, mem_usage, LV_ANIM_OFF);
            lv_label_set_text_fmt(mem_value_label, "%2.0f%%", mem_usage);
            newMemData = false;
        }
        if (newTempData)
        {
            lv_label_set_text_fmt(temp_value_label, "%2.0f°C", temp_value);
            // 更新温度弧线
            uint16_t end_value = 120 + 300 * temp_value / 100.0f;
            lv_color_t arc_color = temp_value > 75 ? lv_color_hex(0xff5d18) : lv_color_hex(0x50ff7d);
            lv_style_set_line_color(&arc_indic_style, LV_STATE_DEFAULT, arc_color);
            lv_obj_add_style(temperature_arc, LV_ARC_PART_INDIC, &arc_indic_style);
            lv_arc_set_end_angle(temperature_arc, end_value);
            newTempData = false;
        }
    }

    // ===== 【新增】同步更新网络速度图表（约在此处插入） =====
    if (uiReady)
    {
        // 检查是否有新的速度数据到来
        bool needChartUpdate = false;
        if (newNetRxData)
        {
            needChartUpdate = true;
            newNetRxData = false;
        }
        if (newNetTxData)
        {
            needChartUpdate = true;
            newNetTxData = false;
        }

        if (needChartUpdate)
        {
            // 更新图表系列
            lv_chart_set_points(chart, ser2, download_series); // 下载（绿色）
            lv_chart_set_points(chart, ser1, upload_series);   // 上传（红色）
            updateChartRange();                                // 自适应 Y 轴范围
            lv_chart_refresh(chart);                           // 立即刷新显示
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
    }
#endif

    // 在 task_cb 时间戳计数
    // // ===== 新增：记录 UI 刷新完成，并调整下一次请求间隔 =====
    lastUiUpdateDone = millis();
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

    tft.begin(); /* TFT init */
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
    lv_obj_set_size(wifi_status_led, 15, 15);                                                             // 设置大小为15x15
    lv_obj_set_pos(wifi_status_led, 135, 220);                                                            // 设置位置在IP地址右侧
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
    // speed_label_color = lv_color_hex(0x838a99);
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
    lv_chart_set_points(chart, ser2, download_series);
    lv_chart_set_points(chart, ser1, upload_series);

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

    // lv_task_t *t = lv_task_create(task_cb, 1000, LV_TASK_PRIO_MID, &test_data);
    lv_task_create(task_cb, 1000, LV_TASK_PRIO_MID, &test_data);
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
        if (IN_USE_PINS[i] == p)
            return true;
    return false;
}

// 3. 在 setup() 最后调用
void setUnusedPinsHiZ()
{
#if !defined(DEBUG_ENABLED) && !defined(DEBUG_ENABLED_0) && \
    !defined(DEBUG_ENABLED_TIME) && !defined(DEBUG_ENABLED_RAM) && \
    !defined(DEBUG_ENABLED_CPU) && !defined(DEBUG_ENABLED_WIFI) && \
    !defined(DEBUG_ENABLED_DATA) && !defined(DEBUG_ENABLED_POWER) && \
    !defined(DEBUG_ENABLED_NTP) && !defined(DEBUG_ENABLED_STATE) && \
    !defined(DEBUG_ENABLED_DEEPSLEEP)

    // NodeMCU v2 可用 GPIO：0,1,2,3,4,5,12,13,14,15,16
    // （6-11 已接 Flash，不可动）
    for (uint8_t p : {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16})
    {
        if (!pinInUse(p))
        {
            pinMode(p, INPUT); // 高阻
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
    !defined(DEBUG_ENABLED_TIME) && !defined(DEBUG_ENABLED_RAM) && \
    !defined(DEBUG_ENABLED_CPU) && !defined(DEBUG_ENABLED_WIFI) && \
    !defined(DEBUG_ENABLED_DATA) && !defined(DEBUG_ENABLED_POWER) && \
    !defined(DEBUG_ENABLED_NTP) && !defined(DEBUG_ENABLED_STATE) && \
    !defined(DEBUG_ENABLED_DEEPSLEEP)

    // Serial.end();           // 关闭 UART0
    pinMode(D10, INPUT); // TXD0 → 高阻1
    pinMode(D9, INPUT);  // RXD0 → 高阻3
    // pinMode(0, INPUT);      // DTR → 高阻 复用引脚不能配置为高阻
    //  若担心外部电路浮动，可再加下拉（可选）
    //  pinMode(1, INPUT_PULLDOWN_16);
    //  pinMode(3, INPUT_PULLDOWN_16);
#else
    Serial.begin(921600); // 76800 115200 128000 230400 256000 460800 921600
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
Serial.printf("RTC marker: 0x%02X, remaining: %lu\n", rtc.marker, (unsigned long)rtc.remaining);
#endif

    if (rtc.marker == 0xA5)
    {
        // RTCData rtc;
        // ESP.rtcUserMemoryRead(RTC_ADDR, (uint32_t *)&rtc, sizeof(rtc));

#ifdef DEBUG_ENABLED_0
Serial.printf("RTC wake-up detected, remaining sleep: %lu seconds, compensatedsleepSec: %s\n",
              (unsigned long)rtc.remaining, (rtc.compensated == 0x01) ? "yes" : "no");
#endif

        // 检查深睡眠总开关
        if (!DEEP_SLEEP_ENABLED)
        {
#ifdef DEBUG_ENABLED_0
            Serial.println("Deep sleep disabled, ignoring RTC wakeup continuation.\n");
#endif
            markRTCWakeup(false);
            rtcNormalWakeup = false;
            // 继续正常启动流程，不调用 actualEnterDeepSleep
        }
        else
        {
            // 核心逻辑修正：只要还有剩余时间，无论多短，都继续睡
            // 移除对 remaining 大小的任何判断
            if (rtc.remaining > 0)
            {
                // 直接进入深睡眠，继续完成剩余睡眠时间
                // 传递已补偿标志，避免对剩余时间再次补偿
                // 这里的 alreadyCompensated 由 RTC 数据中的 compensated 标志决定，确保只补偿一次。
                actualEnterDeepSleep(rtc.remaining, (rtc.compensated == 0x01));
                return; // 代码不会继续执行（如果 actualEnterDeepSleep 成功则复位，否则会重启）
            }
            else
            {
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
    WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &event)
                                { Serial.println("[WiFi Event] Connected to AP"); });
    WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
                                   { Serial.println("[WiFi Event] Disconnected from AP"); });
    WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) 
                                    { Serial.printf("[WiFi Event] Got IP: %s\n", event.ip.toString().c_str()); });
#endif

    UI_init();
    setAppState(APP_INIT);
    setUnusedPinsHiZ();
}

void trySwitchToMonitorPage()
{
    if (uiReady && !isLoggedIn)
    {
        // 检查切换条件：WiFi已连接且NTP时间已同步
        if (appState == APP_OPERATIONAL || appState == APP_GRACE_PERIOD || appState ==APP_PRE_SLEEP) // 时间已同步，WiFi 已连
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
    checkNTP();          // 推进可能存在的 NTP 同步（即使不在 SYNCING 状态）
    // 非阻塞快速推进 HTTP 状态机（仅在活动时，最多 80ms）
    if (httpCtx.state != HTTP_IDLE && httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR) {
        unsigned long sprintEnd = millis() + 80;
        while (httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR && millis() < sprintEnd) {
            handleAsyncHttp();
            yield();      // 允许系统处理 WiFi / 任务，同时自带喂狗
        }
    }
    delay(1);
        // ===== 新增：仅在正常运行状态下进行高频 RSSI 检查（遵循设定的时间参数） =====
    if (appState >= APP_OPERATIONAL) {
        unsigned long checkInterval = (millis() - stateStartTime < FAST_PHASE)
                                      ? RSSI_CHECK_FAST   // 前3分钟：10秒
                                      : RSSI_CHECK_SLOW;  // 之后：60秒

        if (millis() - lastRSSICheck >= checkInterval) {
            int rssi = WiFi.RSSI();
            if (rssi > -100 && rssi < 0) {
                // ① 动态功率调整
                dynamicAdjustTxPower(rssi);

                // ② 弱信号主动漫游检测
                if (rssi < RSSI_WEAK_THRESHOLD) {
                    weakRssiCount++;
                    if (weakRssiCount >= WEAK_RSSI_LIMIT) {
#ifdef DEBUG_ENABLED_0
                        Serial.printf("Weak RSSI %d for %d times, triggering reconnect...\n", rssi, weakRssiCount);
#endif
                        if (appState >= APP_OPERATIONAL) {  // 再次确认状态
                            closeNetdataConnection();
                            httpCtx.state = HTTP_IDLE;
                            currentRequestPhase = REQ_IDLE;
                            weakRssiCount = 0;

                            WiFi.disconnect(false);
                            delay(1);
                            ESP.restart();
                        }
                    }
                } else {
                    weakRssiCount = 0;
                }
            }
            lastRSSICheck = millis();
        }
    }
    // 当 WiFi 已连接、NTP 已同步、UI 就绪且尚未登录时，自动从登录页切换到监控页
    trySwitchToMonitorPage();
    // ---------- 2. 检查异步请求是否完成 ----------
    bool success;
    if (isAsyncHttpDone(success))
    {
        delay(1); // 立即给 WiFi 进入休眠的机会
        switch (currentRequestPhase)
        {
        case REQ_BATCH:
            if (success)
            {
#ifdef DEBUG_ENABLED
                Serial.println("Batch data request succeeded");
#endif
                newCPUData = true;
                newNetRxData = true;
                newNetTxData = true;
                // 通过requestCycleCount计数器判断是否更新了完整数据
                if (requestCycleCount % FULL_REQUEST_INTERVAL == 0)
                {
                    newMemData = true;
                    newTempData = true;
                    requestCycleCount = 0;
                }
                requestCycleCount++; // 每次请求计数+1
            }
            else
            {
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

    if (appState >= APP_OPERATIONAL) {
        static unsigned long lastBatchRequest = 0;
        // 仅当：1) 没有请求在进行；2) 上一批数据已被显示消费，才发起新请求
        bool hasUnshownData = (newCPUData || newNetRxData || newNetTxData);
        if (millis() - lastBatchRequest >= batchRequestInterval) {
            if (currentRequestPhase == REQ_IDLE && !hasUnshownData &&
                (millis() - lastUiUpdateDone >= UI_WIFI_MARGIN)) {
                bool requestStarted = false;

                // 每 X 次中，第 X 次执行完整请求（包含 mem、temp），其余只请求 CPU+网络
                if (requestCycleCount % FULL_REQUEST_INTERVAL == 0) {
                    requestStarted = startBatchNetDataRequest(); // 完整请求
                }
                else {
                    requestStarted = startFastNetDataRequest(); // 快速请求
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

    // 所有状态下都需要处理的全局任务
    handleWiFiHardware(); // 该函数会设置状态到 APP_WIFI_CONNECTING

    // 根据当前状态执行相应的操作
switch (appState) {
    case APP_INIT:
        wifiAttempts = 0;
        wifiConnectedTime = 0;
        waitingRestartTime = 0;
        setAppState(APP_WIFI_CONNECTING);
        break;
    case APP_WIFI_CONNECTING:
    {
        // ---------- 进入该状态时重置所有计数器（利用 stateStartTime 变化检测） ----------
        static unsigned long lastStateEnter = 0; // 记录上一次进入时的 stateStartTime
        if (lastStateEnter != stateStartTime)
        {
            // stateStartTime 在 setAppState() 中更新
            lastStateEnter = stateStartTime;
            // wifiAttempts = 0;
            wifiConnectedTime = 0;
            waitingRestartTime = 0;
            wifiAttemptStart = 0; // 立即允许第一次重试
#ifdef DEBUG_ENABLED_WIFI
            Serial.println("[WiFi] Entered WIFI_CONNECTING state, Counters Reset");
#endif
        }

        // ---------- 情况 A：WiFi 已连接 ----------
        if (WiFi.status() == WL_CONNECTED)
        {
            waitingRestartTime = 0; // 取消等待重启计时
            if (wifiConnectedTime == 0)
            {
                wifiConnectedTime = millis(); // 记录连接成功时刻
#ifdef DEBUG_ENABLED_WIFI
Serial.printf("[WiFi] Connected, Waiting %d ms to stabilize...\n", WIFI_POST_CONNECT_DELAY_MS );
#endif
            }
            // 等待 WIFI_POST_CONNECT_DELAY_MS 秒稳定期，防止瞬时获取 IP 后立刻断开
            else if (millis() - wifiConnectedTime >= WIFI_POST_CONNECT_DELAY_MS)
            {
#ifdef DEBUG_ENABLED_WIFI
                Serial.println("[WiFi] Stable, moving to NTP sync.");
#endif
                resetDynamicPowerState();     // 稳定后重置功率状态
                tryStartNTPSync(true);        // ★ 强制发起 NTP 同步
                setAppState(APP_NTP_SYNCING); // 切换至 NTP 同步状态
                wifiAttempts = 1;             // 复位为软连接状态
                break;
            }
        }
        // ---------- 情况 B：WiFi 未连接 ----------
        else
        {
            wifiConnectedTime = 0; // 清除稳定计时

            // 确定本次重试将采用的复位类型（与后续 actual 连接一致）
            bool forceReset =(wifiAttempts == 0) || (wifiAttempts == 1) || (wifiAttempts >= 3);
            unsigned long retryInterval = forceReset ? WIFI_RECONNECT_HARD_INTERVAL : WIFI_RECONNECT_SOFT_INTERVAL;

            // 达到重试间隔后发起连接以及首次连接一定是硬连接
            if ((millis() - wifiAttemptStart >= retryInterval) || (wifiAttempts == 0))
            {
                if(wifiAttempts == 1) {
                    forceReset = false;
                }
#ifdef DEBUG_ENABLED_WIFI
                    unsigned long waited = millis() - wifiAttemptStart;
                    Serial.printf("[WiFi] Attempt %d/%d (type=%s, interval=%lums, waited=%lums)\n",
                                  wifiAttempts + 1, MAX_WIFI_RECONNECT_ATTEMPTS,
                                  forceReset ? "HARD" : "SOFT",
                                  retryInterval, waited);
#endif

                connectWiFi(forceReset);
                wifiAttemptStart = millis();
                wifiAttempts++;
            }

            // ---------- 超过最大重试次数：进入 10 秒重启倒计时 ----------
            if (wifiAttempts > MAX_WIFI_RECONNECT_ATTEMPTS)
            {
                if (waitingRestartTime == 0)
                {
                    waitingRestartTime = millis();
#ifdef DEBUG_ENABLED_WIFI
                    Serial.printf("[WiFi] Max retries exceeded, restarting in 10s...\n");
#endif
                }
                else
                {
                    unsigned long elapsed = millis() - waitingRestartTime;
                    if (elapsed >= 10000)
                    {
#ifdef DEBUG_ENABLED_WIFI
                        Serial.println("[WiFi] Restarting now.");
#endif
                        ESP.restart();
                    }
                    // 每秒输出一次倒计时（提高可观测性）
                    else if (elapsed % 1000 < 20)
                    { // 20ms 窗口，防止频繁打印
#ifdef DEBUG_ENABLED_WIFI
                        Serial.printf("[WiFi] Restart in %d s...\n", 10 - (int)(elapsed / 1000));
#endif
                    }
                    yield(); // 倒计时期间喂狗
                }
            }
        }
        break;
    }
    case APP_NTP_SYNCING:
        if (ntpCompleted)
        {
#ifdef DEBUG_ENABLED_NTP
            Serial.println("[NTP] Sync COMPLETED, evaluating sleep...");
#endif
            TimeCheckResult tc = checkSleepTime();
            if (rtcNormalWakeup && tc.inSleepWindow) {
                prepareSleepOrForceSync();
            }
            else if (!rtcNormalWakeup && tc.inSleepWindow) {
                gracePeriodEnd = millis() + POST_POWERON_GRACE_MS;
                setAppState(APP_GRACE_PERIOD);
            }
            else {
                setAppState(APP_OPERATIONAL);
            }
            rtcNormalWakeup = false;
        }
        else if (ntpFailed) {
#ifdef DEBUG_ENABLED_NTP
            Serial.printf("[NTP] Sync FAILED (consecutive failures: %u)\n", ntpConsecutiveFailures);
#endif
            ntpFailed = false;
            if (ntpConsecutiveFailures >= NTP_MAX_FAILURES) {
#ifdef DEBUG_ENABLED_NTP
                Serial.println("[NTP] Max failures reached -> FAULT");
#endif
                setAppState(APP_FAULT);
            }
        }
        //失败次数达上限直接移交故障处理
        if (ntpConsecutiveFailures >= NTP_MAX_FAILURES) {
            setAppState(APP_FAULT);
            break;
        }
        if (!ntpCompleted && appState != APP_FAULT) {
            tryStartNTPSync(false); // 内部已含日志
#ifdef DEBUG_ENABLED_NTP
        Serial.println("[NTP] tryStartNTPSync() returned false (waiting)");
#endif
        }
        break;

    case APP_OPERATIONAL:
    {
        static unsigned long lastSleepCheck = 0;

        // ── 睡眠窗口检测（仅在启用深睡眠且时间有效时运行）──
        if (DEEP_SLEEP_ENABLED && ntpCompleted)
        {
            uint8_t h = getCurrentHourLocal();
            uint8_t m = getCurrentMinuteLocal();
            // 动态调整检查间隔：距下一睡眠窗口 <= 3分钟时使用短间隔
            unsigned long checkInt = (secondsToNextSleepWindow(h, m) <= 180)
                                         ? CHECK_INTERVAL_SHORT
                                         : CHECK_INTERVAL_LONG;
#ifdef DEBUG_ENABLED_DEEPSLEEP
            static unsigned long lastLogTime2 = 0;
            if (millis() - lastLogTime2 > 60000)
            { // 每60秒最多打印一次
                lastLogTime2 = millis();
                unsigned long dist = secondsToNextSleepWindow(h, m);
                Serial.printf("[DEEPSLEEP] Operational: %02d:%02d, dist=%lu s, checkInt=%lu ms\n",
                              h, m, dist, checkInt);
            }
#endif
            if (millis() - lastSleepCheck >= checkInt)
            {
                TimeCheckResult tc = checkSleepTime();
                if (tc.inSleepWindow && tc.sleepSeconds > 0)
                {
                    prepareSleepOrForceSync(); // 统一入口，自动处理强制NTP同步
                }
                lastSleepCheck = millis();
            }
        }

        // 数据请求由 loop() 前面的块处理，无需 tryStartDataRequest
        // 定期 NTP 同步（间隔=NTP_INTERVAL）
        if (millis() - lastNTPSyncMillis >= NTP_INTERVAL)
        {
            tryStartNTPSync(true);
        }
        break;
    }

    case APP_GRACE_PERIOD:
        // 动态检查是否已离开睡眠窗口
        {
            TimeCheckResult tc = checkSleepTime();
            if (!tc.inSleepWindow) {
                setAppState(APP_OPERATIONAL);
                break;
            }
        }
        // 定期 NTP 同步（间隔=NTP_INTERVAL）
        if (millis() - lastNTPSyncMillis >= NTP_INTERVAL) {
            tryStartNTPSync(true);
        }
        if (millis() > gracePeriodEnd) {
            TimeCheckResult tc = checkSleepTime();
            if (tc.inSleepWindow && tc.sleepSeconds > 0) {
                prepareSleepOrForceSync();
            } else {
                setAppState(APP_OPERATIONAL);
            }
        }
        break;

    case APP_PRE_SLEEP:
    {
        // 此状态仅应在 forceNTPSyncBeforeSleep==true 时进入。
        // 若标志意外为 false，退回正常运行并记录错误。
        if (!forceNTPSyncBeforeSleep)
        {
#ifdef DEBUG_ENABLED_DEEPSLEEP
            Serial.println("[DEEPSLEEP] ERROR: PRE_SLEEP without force flag! Fallback to OPERATIONAL.");
#endif
            setAppState(APP_OPERATIONAL);
            break;
        }

        // 等待强制 NTP 同步完成或超时
        if (ntpCompleted)
        {
#ifdef DEBUG_ENABLED_DEEPSLEEP
            Serial.println("[DEEPSLEEP] Force NTP sync done, entering deep sleep.");
#endif
            forceNTPSyncBeforeSleep = false;
            enterDeepSleep();
        }
        else if (millis() - forceSyncStartTime > 60000)
        {
#ifdef DEBUG_ENABLED_DEEPSLEEP
            Serial.printf("[DEEPSLEEP] Force NTP sync timeout (%lu ms), entering deep sleep anyway.\n",
                          millis() - forceSyncStartTime);
#endif
            forceNTPSyncBeforeSleep = false;
            enterDeepSleep();
        }
        // 否则继续等待，checkNTP() 在后台推进
        break;
    }
    case APP_DEEP_SLEEP:
    // 不应到达这里，若意外出现则直接进入睡眠处理
    break;
    case APP_FAULT:
        if (millis() - stateStartTime > 5000) ESP.restart();
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
            if (!isWiFiTransactionActive() && (hasNewData || (millis() - lastRefreshTime >= LOGGED_IN_REFRESH_INTERVAL))) {
                    lv_task_handler();
                    lastRefreshTime = millis();
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
