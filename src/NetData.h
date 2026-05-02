#ifndef __NETDATA_H
#define __NETDATA_H

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <string>

#ifndef CHART_MEM_X
#define CHART_MEM_X 1024.0
#endif

// 复用单个 WiFiClient，避免连接池开销
static WiFiClient netdataClient;

bool ensureNetdataConnection();

enum HttpRequestState {
    HTTP_IDLE,           // 空闲
    HTTP_CONNECTING,     // 正在 TCP 连接
    HTTP_SENDING,        // 发送请求头
    HTTP_WAITING_RESP,   // 等待响应
    HTTP_READING_STATUS, // 读取状态行
    HTTP_READING_HEADERS,// 跳过头部
    HTTP_READING_BODY,   // 读取 JSON 体
    HTTP_COMPLETED,      // 完成（成功或失败）
    HTTP_ERROR
};

class NetChartData
{
public:
    int api;
    String id;
    String name;

    int view_update_every;
    int update_every;
    long first_entry;
    long last_entry;
    long before;
    long after;
    String group;
    String options_0;
    String options_1;

    JsonArray dimension_names;
    JsonArray dimension_ids;
    JsonArray latest_values;
    JsonArray view_latest_values;
    int dimensions;
    int points;
    String format;
    JsonArray result;
    double min;
    double max;
};

struct AsyncHttpContext {
    HttpRequestState state = HTTP_IDLE;
    WiFiClient* client = nullptr;
    String chartID;
    String dimensionFilter;
    NetChartData* resultData;
    unsigned long lastActionTime;
    String httpRequest;
    String lineBuffer;        // 行缓冲区
    int contentLength = -1;   // 存储 Content-Length
    int bodyRead = 0;         // 已读字节计数
    String bodyBuffer;        //存储完整响应体
    bool success = false;
    bool forceReconnect = false; //HTTP_ERROR标记
};

extern AsyncHttpContext httpCtx;  // 全局或静态
static IPAddress cachedServerIP;
static bool ipCached = false;

// ===== 修改点 2：新增批量响应解析函数 =====
// 注意：需要 extern 声明 main.ino 中的全局变量
extern double cpu_usage;
extern double mem_usage;
extern double temp_value;
extern double up_speed;
extern double down_speed;
extern lv_coord_t upload_serise[10];
extern lv_coord_t download_serise[10];
extern lv_coord_t up_speed_max;
extern lv_coord_t down_speed_max;

// 前置声明（main.ino 中的辅助函数）
lv_coord_t updateNetSeries(lv_coord_t* series, double speed);

// ===== 新增函数：解析 format=array 的批量响应 =====
// 替代原来的 parseBatchNetDataResponse
bool parseBatchArrayResponse(const String& jsonStr) {
#ifdef DEBUG_ENABLED
// 打印原始响应 ===
Serial.println("=== RAW BATCH RESPONSE ===");
Serial.println(jsonStr);
Serial.println("=== END ===");
#endif
    // 缓冲区必须足够大，这里用 6144 字节（根据内存情况可微调）
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        #ifdef DEBUG_ENABLED_0
        Serial.printf("Batch parse error: %s\n", error.c_str());
        if (error == DeserializationError::NoMemory)
            Serial.printf("JSON size: %d bytes, heap: %d\n", jsonStr.length(), ESP.getFreeHeap());
        #endif
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (!root.containsKey("latest_values") || !root.containsKey("dimension_names")) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Missing required keys in batch response");
        #endif
        return false;
    }

    // ===== 关键修复：显式转换（ArduinoJson v7 要求）=====
    JsonArray latest   = root["latest_values"].as<JsonArray>();
    JsonArray dimNames = root["dimension_names"].as<JsonArray>();

    if (latest.size() != dimNames.size()) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Dimension count mismatch");
        #endif
        return false;
    }

    // 遍历维度，根据名称分发
    for (size_t i = 0; i < dimNames.size(); i++) {
        const char* dim = dimNames[i];
        double value    = latest[i];

        if (strcmp(dim, DIM_RX) == 0) {
            down_speed = fabs(value) / 8.0;
            down_speed_max = updateNetSeries(download_serise, down_speed);
        } else if (strcmp(dim, DIM_TX) == 0) {
            up_speed = fabs(value) / 8.0;
            up_speed_max = updateNetSeries(upload_serise, up_speed);
        } else if (strcmp(dim, DIM_CPU) == 0) {
            cpu_usage = value;
        } else if (strstr(dim, DIM_MEM) != nullptr) {
            // mem.available 维度可能叫 "avail" 或 "MemAvailable"
            double avail = value;
            mem_usage = 100.0 * (1.0 - avail / CHART_MEM_X);
        } else if (strstr(dim, DIM_TEMP) != nullptr) {
            temp_value = value;
        }
        // 忽略未知维度
    }

    #ifdef DEBUG_ENABLED
    Serial.printf("Parsed CPU:%.2f MEM:%.2f TEMP:%.2f Up:%.2f Down:%.2f\n",
                  cpu_usage, mem_usage, temp_value, up_speed, down_speed);
    #endif
    return true;
}

// ===== 修改点 1：新增批量请求启动函数 =====
bool startBatchNetDataRequest() {
    // 检查是否有请求正在进行
    if (httpCtx.state != HTTP_IDLE && httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR) {
        return false;
    }
    
    // 确保连接可用
    if (!ensureNetdataConnection()) {
        return false;
    }
    
    // 重置上下文
    httpCtx = AsyncHttpContext();
    // 组合所有需要监控的图表 ID（用逗号分隔）
    // 修正为使用宏拼接
    httpCtx.chartID = String(CHART_NET) + "," +
                    String(CHART_CPU) + "," +
                    String(CHART_MEM) + "," +
                    String(CHART_TEMP);
    httpCtx.dimensionFilter = ""; // 批量请求时不使用维度过滤，在解析时处理
    httpCtx.state = HTTP_CONNECTING;
    httpCtx.lastActionTime = millis();
    httpCtx.client = &netdataClient;
    
    // 构建批量请求 URL（一次性获取所有图表的最新一个数据点）
    String reqRes = "/api/v1/data?chart=" + httpCtx.chartID + 
                "&format=array&points=1&group=average&gtime=0&options=s%7Cjsonwrap%7Cnonzero&after=-2";
    // 【新增】限定只返回我们需要解析的维度，大幅减小响应体积
    // 注意：维度名必须与 NetData 中实际名称一致，
    // 请根据解析函数（ parseBatchArrayResponse ）中使用的维度名调整。
    // 当前解析中使用的维度为：
    //   CPU: "DIM_CPU"  |  网络: "DIM_RX","DIM_TX"  |  内存: 含 "DIM_MEM" |  温度: 含 "DIM_TEMP"
    // 下面字符串包含了这些关键字的常见精确名称，如果与实际不符，请通过串口输出一次完整响应调整。
    reqRes += "&dimensions="DIM_RX","DIM_TX","DIM_CPU","DIM_MEM","DIM_TEMP"";
    httpCtx.httpRequest = "GET " + reqRes + " HTTP/1.1\r\n" + 
                          "Host: " + String(NETDATA_SERVER_IP) + "\r\n" + 
                          "Connection: keep-alive\r\n" +
                          "User-Agent: RM\r\n" +
                          "Accept: application/json\r\n\r\n";
    return true;
}

// ===== 新增：快速请求（仅 CPU + 网络速度）=====
// 只获取 system.cpu 和 net.wan，不获取 mem 和 temp，大幅降低数据量
bool startFastNetDataRequest() {
    // 检查是否有请求正在进行
    if (httpCtx.state != HTTP_IDLE && httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR) {
        return false;
    }
    
    // 确保连接可用
    if (!ensureNetdataConnection()) {
        return false;
    }
    
    // 重置上下文
    httpCtx = AsyncHttpContext();
    //只请求 CPU 和网络速度
    httpCtx.chartID = String(CHART_NET) + "," + String(CHART_CPU);
    httpCtx.dimensionFilter = "";
    httpCtx.state = HTTP_CONNECTING;
    httpCtx.lastActionTime = millis();
    httpCtx.client = &netdataClient;
    
    // 构建请求 URL（用法与完整请求一致）
    String reqRes = "/api/v1/data?chart=" + httpCtx.chartID + 
                    "&format=array&points=1&group=average&gtime=0&options=s%7Cjsonwrap%7Cnonzero&after=-2"+
                    "&dimensions="DIM_RX","DIM_TX","DIM_CPU"";  // <-- 新增此行
    httpCtx.httpRequest = "GET " + reqRes + " HTTP/1.1\r\n" + 
                          "Host: " + String(NETDATA_SERVER_IP) + "\r\n" + 
                          "Connection: keep-alive\r\n" +
                          "User-Agent: RM\r\n" +
                          "Accept: application/json\r\n\r\n";
    return true;
}

/**
 * 获取可用的 NetData 客户端连接
 * 自动处理连接复用和重建
 * @return true 表示连接就绪，false 表示连接失败
 */
bool ensureNetdataConnection() {
    // 仅靠 connected() 判定，不清掉可能可用的连接
    if (netdataClient.connected()) {
        // 清空残留数据
        while (netdataClient.available()) netdataClient.read();
        return true;
    }
    
    netdataClient.stop();
    delay(1);
    netdataClient.setTimeout(3000);
    
    IPAddress ip;
    if (ipCached) {
        ip = cachedServerIP;
    } else {
        if (WiFi.hostByName(NETDATA_SERVER_IP, ip, 500)) {
            cachedServerIP = ip;
            ipCached = true;
        } else {
            return false;
        }
    }
    
    if (!netdataClient.connect(ip, NETDATA_SERVER_PORT)) {
        return false;
    }

    // 启用 Nagle 算法，让小包（如 HTTP 请求）即时发出，降低缓冲占用
    netdataClient.setNoDelay(false);
    
    #ifdef DEBUG_ENABLED_0
    Serial.println("New TCP connection");
    #endif
    return true;
}

/**
 * 强制关闭 NetData 连接（例如在进入深睡眠前调用）
 */
void closeNetdataConnection() {
    netdataClient.stop();
}

// ---------- 异步 HTTP 状态机推进函数 ----------
inline void handleAsyncHttp() {
    AsyncHttpContext& ctx = httpCtx;
    unsigned long now = millis();
    const unsigned long TIMEOUT_MS = 5000;
    
    // 🔵 新增：记录是否为分块传输
    static bool isChunked = false;

    switch (ctx.state) {
        case HTTP_IDLE:
        case HTTP_COMPLETED:
        case HTTP_ERROR:
            return;

        case HTTP_CONNECTING: {
            // ===== 若连接已存在，直接复用，跳到发送阶段 =====
            if (ctx.client->connected()) {
                ctx.state = HTTP_SENDING;
                ctx.lastActionTime = now;
                break;
            }
            
            IPAddress ip;
            if (ipCached) {
                ip = cachedServerIP;
            } else {
                if (WiFi.hostByName(NETDATA_SERVER_IP, ip, 500)) {
                    cachedServerIP = ip;
                    ipCached = true;
                } else {
                    ctx.state = HTTP_ERROR;
                    break;
                }
            }
            
            if (ctx.client->connect(ip, NETDATA_SERVER_PORT)) {
                ctx.state = HTTP_SENDING;
                ctx.lastActionTime = now;
            } else if (now - ctx.lastActionTime > 2000) {
                ctx.state = HTTP_ERROR;
            }
            break;
        }

        case HTTP_SENDING: {
            size_t written = ctx.client->print(ctx.httpRequest);
#ifdef DEBUG_ENABLED
            Serial.println("--- HTTP Request ---");
            Serial.println(ctx.httpRequest);
            Serial.println("-------------------");
#endif
            if (written == ctx.httpRequest.length()) {
                ctx.client->flush();
                // 🔽 新增：重置分块标志，避免上次请求污染
                isChunked = false;
                ctx.state = HTTP_WAITING_RESP;
                ctx.lastActionTime = now;
            } else if (now - ctx.lastActionTime > 2000) {
                ctx.state = HTTP_ERROR;
            }
            break;
        }

        case HTTP_WAITING_RESP: {
            if (ctx.client->available()) {
                ctx.state = HTTP_READING_STATUS;
                ctx.lineBuffer = "";
            } else if (now - ctx.lastActionTime > TIMEOUT_MS) {
                ctx.state = HTTP_ERROR;
            }
            break;
        }

        case HTTP_READING_STATUS: {
            if (ctx.client->available()) {
                char c = ctx.client->read();
                if (c == '\n') {
                    if (ctx.lineBuffer.indexOf("200 OK") == -1) {
                        ctx.state = HTTP_ERROR;
                    } else {
                        ctx.state = HTTP_READING_HEADERS;
                        ctx.lineBuffer = "";
                    }
                } else if (c != '\r') {
                    ctx.lineBuffer += c;
                }
            } else if (now - ctx.lastActionTime > TIMEOUT_MS) {
                ctx.state = HTTP_ERROR;
            }
            break;
        }

        case HTTP_READING_HEADERS: {
            int readCount = 0;
            while (ctx.client->available()) {
                char c = ctx.client->read();
                if (c == '\n') {
#ifdef DEBUG_ENABLED
                    Serial.print("HDR: ");
                    Serial.println(ctx.lineBuffer);
#endif
                    if (ctx.lineBuffer.length() == 0 || ctx.lineBuffer == "\r") {
                        // 头部结束，准备读取 body
                        ctx.state = HTTP_READING_BODY;
                        ctx.bodyRead = 0;
                        break;
                    }
                    // 提取 Content-Length
                    if (ctx.lineBuffer.startsWith("Content-Length:")) {
                        String val = ctx.lineBuffer.substring(15);
                        val.trim();
                        ctx.contentLength = val.toInt();
                    }
                    // 🔵 新增：识别分块传输编码
                    else if (ctx.lineBuffer.startsWith("Transfer-Encoding:")) {
                        if (ctx.lineBuffer.indexOf("chunked") != -1) {
                            isChunked = true;
                        }
                    }
                    ctx.lineBuffer = "";
                } else if (c != '\r') {
                    ctx.lineBuffer += c;
                }
                // if (++readCount >= 128) {
                //     yield();
                //     readCount = 0;
                // }
            }
            if (now - ctx.lastActionTime > TIMEOUT_MS && ctx.state == HTTP_READING_HEADERS) {
                ctx.state = HTTP_ERROR;
            }
            break;
        }

        case HTTP_READING_BODY: {
            const int MAX_BODY_SIZE = 4096;

            // ========== 分块传输编码 ==========
            if (isChunked) {
                while (ctx.client->available() && ctx.bodyRead < MAX_BODY_SIZE) {
                    String line = ctx.client->readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) continue;
                    unsigned long chunkSize = strtoul(line.c_str(), NULL, 16);
                    if (chunkSize == 0) {
                        // 终止块，解析并结束
                        if (parseBatchArrayResponse(ctx.bodyBuffer)) {
                            ctx.success = true;
                        } else {
                            ctx.success = false;
                        }
                        ctx.state = HTTP_COMPLETED;
                        break;
                    }
                    int remaining = chunkSize;
                    while (remaining > 0 && ctx.client->available()) {
                        int toRead = min(remaining, 1024);
                        char chunkData[1025];                        // 栈数组避免堆碎片
                        int len = ctx.client->readBytes(chunkData, toRead);
                        if (len > 0) {
                            chunkData[len] = '\0';
                            ctx.bodyBuffer += chunkData;
                            ctx.bodyRead += len;
                            remaining -= len;
                        } else {
                            break;
                        }
                    }
                    // 跳过 chunk 末尾 \r\n
                    if (ctx.client->available() >= 2) {
                        ctx.client->read(); ctx.client->read();
                    }
                    ctx.lastActionTime = now;
                    if (ctx.bodyRead >= MAX_BODY_SIZE) break;
                }
                if (ctx.state != HTTP_COMPLETED && ctx.bodyRead >= MAX_BODY_SIZE) {
                    ctx.state = HTTP_ERROR;
                }
            }
            // ========== 常规 Content-Length ==========
            else if (ctx.contentLength > 0) {
                while (ctx.client->available() && ctx.bodyRead < ctx.contentLength && ctx.bodyRead < MAX_BODY_SIZE) {
                    int toRead = ctx.contentLength - ctx.bodyRead;
                    if (toRead > 1024) toRead = 1024;
                    uint8_t buf[1024];
                    int len = ctx.client->read(buf, toRead);
                    if (len > 0) {
                        ctx.bodyBuffer.concat((char*)buf, len);
                        ctx.bodyRead += len;
                        ctx.lastActionTime = now;
                    } else {
                        break;
                    }
                }
                if (ctx.bodyRead >= ctx.contentLength) {
                    if (parseBatchArrayResponse(ctx.bodyBuffer)) {
                        ctx.success = true;
                    } else {
                        ctx.success = false;
                    }
                    ctx.state = HTTP_COMPLETED;
                } else if (now - ctx.lastActionTime > TIMEOUT_MS) {
                    ctx.state = HTTP_ERROR;
                }
            }
            // ========== 无长度无分块（保底） ==========
            else {
                while (ctx.client->available() && ctx.bodyRead < MAX_BODY_SIZE) {
                    char c = ctx.client->read();
                    ctx.bodyBuffer += c;
                    ctx.bodyRead++;
                    ctx.lastActionTime = now;
                }
                if (!ctx.client->connected() && ctx.bodyBuffer.length() > 0) {
                    if (parseBatchArrayResponse(ctx.bodyBuffer)) {
                        ctx.success = true;
                    } else {
                        ctx.success = false;
                    }
                    ctx.state = HTTP_COMPLETED;
                } else if (now - ctx.lastActionTime > TIMEOUT_MS) {
                    ctx.state = HTTP_ERROR;
                }
            }
            break;
        }
    }
    
    if (ctx.state == HTTP_ERROR) {
        ctx.client->stop();
        ctx.success = false;
        ctx.state = HTTP_COMPLETED;
        ctx.forceReconnect = true;
    }
}

// ---------- 检查异步请求是否完成 ----------
inline bool isAsyncHttpDone(bool& success) {
    if (httpCtx.state == HTTP_COMPLETED) {
        success = httpCtx.success;
        httpCtx.state = HTTP_IDLE;
        return true;
    }
    return false;
}

#endif
