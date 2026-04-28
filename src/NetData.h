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
static unsigned long lastNetdataRequestTime = 0;
static const unsigned long CONNECTION_IDLE_TIMEOUT = 3; // 3秒空闲后关闭连接

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
// 在 parseNetDataResponseFromString 函数后面添加以下函数
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

bool parseBatchNetDataResponse(const String& jsonStr) {
#ifdef DEBUG_ENABLED
    // 打印原始响应 ===
    Serial.println("=== RAW BATCH RESPONSE ===");
    Serial.println(jsonStr);
    Serial.println("=== END ===");
#endif
    DynamicJsonDocument doc(2560);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        #ifdef DEBUG_ENABLED_0
        Serial.print("Batch parse failed: ");
        Serial.println(error.f_str());
        #endif
        return false;
    }

    JsonArray dimNames = doc["dimension_names"].as<JsonArray>();
    JsonArray chartIds = doc["chart_ids"].as<JsonArray>();
    JsonObject resultObj = doc["result"];
    JsonArray data = resultObj["data"].as<JsonArray>();
    
    if (!dimNames || !chartIds || !data || data.size() == 0) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Invalid batch response structure");
        #endif
        return false;
    }
    
    JsonArray row = data[0].as<JsonArray>();
    if (row.size() < dimNames.size() + 1) return false;
    
    // 标志位，记录是否成功获取到温度
    bool gotTemperature = false;
    
    for (size_t i = 0; i < dimNames.size(); i++) {
        const char* chart = chartIds[i].as<const char*>();
        const char* dim = dimNames[i].as<const char*>();
        double value = row[i + 1].as<double>();
        
        if (strcmp(chart, "system.cpu") == 0) {
            if (strcmp(dim, "system") == 0) {
                cpu_usage = value;
            }
        }
        else if (strcmp(chart, "mem.available") == 0) {
            if (strstr(dim, "avail") != nullptr) {
                mem_usage = 100.0 * (1.0 - value / CHART_MEM_X);
            }
        }
        else if (strstr(chart, "sensors.") != nullptr || strstr(chart, "temp") != nullptr) {
            // 匹配任意温度相关图表，取第一个值
            if (!gotTemperature) {
                temp_value = value;
                gotTemperature = true;
            }
        }
        else if (strcmp(chart, "net.wan") == 0) {
            double speed_bytes = fabs(value) / 8.0; // 使用绝对值，确保速度为正
            if (strcmp(dim, "received") == 0) {
                down_speed = speed_bytes;
                down_speed_max = updateNetSeries(download_serise, speed_bytes);
            } else if (strcmp(dim, "sent") == 0) {
                up_speed = speed_bytes;
                up_speed_max = updateNetSeries(upload_serise, speed_bytes);
            }
        }
    }
    
    
    #if defined(DEBUG_ENABLED_DATAI) || defined(DEBUG_ENABLED)
    Serial.println("=== Parsed Data ===");
    Serial.printf("CPU: %.2f%%\n", cpu_usage);
    Serial.printf("MEM: %.2f%%\n", mem_usage);
    Serial.printf("TEMP: %.2f C\n", temp_value);
    Serial.printf("UP: %.2f K/s, DOWN: %.2f K/s\n", up_speed, down_speed);
    Serial.println("===================");
    #endif
    
    return true;
}

// ===== 修改点 1：新增批量请求启动函数 =====
// 在 NetData.h 末尾（startAsyncNetDataRequest 后面）添加以下函数
bool startBatchNetDataRequest(NetChartData& dummy) {
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
    httpCtx.chartID = String(CHART_CPU) + "," +
                    String(CHART_MEM) + "," +
                    String(CHART_TEMP) + "," +
                    String(CHART_NET);
    httpCtx.dimensionFilter = ""; // 批量请求时不使用维度过滤，在解析时处理
    httpCtx.resultData = &dummy;   // 这里传入一个占位引用，实际数据解析由专用函数完成
    httpCtx.state = HTTP_CONNECTING;
    httpCtx.lastActionTime = millis();
    httpCtx.client = &netdataClient;
    
    // 构建批量请求 URL（一次性获取所有图表的最新一个数据点）
    String reqRes = "/api/v1/data?chart=" + httpCtx.chartID + 
                    "&format=json&points=1&group=average&gtime=0&options=s%7Cjsonwrap%7Cnonzero&after=-2";
    // 【新增】限定只返回我们需要解析的维度，大幅减小响应体积
    // 注意：维度名必须与 NetData 中实际名称一致，
    // 请根据解析函数（parseBatchNetDataResponse）中使用的维度名调整。
    // 当前解析中使用的维度为：
    //   CPU: "system"  |  网络: "received","sent"  |  内存: 含 "avail"  |  温度: 含 "temp"
    // 下面字符串包含了这些关键字的常见精确名称，如果与实际不符，请通过串口输出一次完整响应调整。
    reqRes += "&dimensions=received,sent,temp,system,avail";
    httpCtx.httpRequest = "GET " + reqRes + " HTTP/1.1\r\n" + 
                          "Host: " + String(NETDATA_SERVER_IP) + "\r\n" + 
                          "Connection: keep-alive\r\n" +
                          "User-Agent: RM\r\n" +
                          "Accept: application/json\r\n\r\n";
    return true;
}

// ===== 新增：快速请求（仅 CPU + 网络速度）=====
// 只获取 system.cpu 和 net.wan，不获取 mem 和 temp，大幅降低数据量
bool startFastNetDataRequest(NetChartData& dummy) {
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
    httpCtx.chartID = String(CHART_CPU) + "," + String(CHART_NET);
    httpCtx.dimensionFilter = "";
    httpCtx.resultData = &dummy;
    httpCtx.state = HTTP_CONNECTING;
    httpCtx.lastActionTime = millis();
    httpCtx.client = &netdataClient;
    
    // 构建请求 URL（用法与完整请求一致）
    String reqRes = "/api/v1/data?chart=" + httpCtx.chartID + 
                    //"&format=json&points=1&group=average&gtime=0&options=s%7Cjsonwrap%7Cnonzero&after=-2";
                    "&format=json&points=1&group=average&gtime=0&options=s%7Cjsonwrap%7Cnonzero&after=-2"+
                    "&dimensions=system,received,sent";  // <-- 新增此行
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

    // 禁用 Nagle 算法，让小包（如 HTTP 请求）即时发出，降低缓冲占用
    //netdataClient.setNoDelay(true);
    
    #ifdef DEBUG_ENABLED_0
    Serial.println("New TCP connection");
    #endif
    return true;
}

/**
 * 标记一次请求已完成，更新最后使用时间
 */
void markNetdataRequestDone() {
    lastNetdataRequestTime = millis();
}

/**
 * 强制关闭 NetData 连接（例如在进入深睡眠前调用）
 */
void closeNetdataConnection() {
    netdataClient.stop();
}

// 解析NetData响应
// 从 String 解析 NetData 响应（零拷贝流，直接传入 JSON 字符串）
bool parseNetDataResponseFromString(const String& jsonStr, NetChartData& data) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        #ifdef DEBUG_ENABLED_0
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        #endif
        return false;
    }

    data.api = doc["api"];
    data.id = doc["id"].as<String>();
    data.name = doc["name"].as<String>();
    data.view_update_every = doc["view_update_every"];
    data.update_every = doc["update_every"];
    data.first_entry = doc["first_entry"];
    data.last_entry = doc["last_entry"];
    data.after = doc["after"];
    data.before = doc["before"];
    data.group = doc["group"].as<String>();
    data.options_0 = doc["options"][0].as<String>();
    data.options_1 = doc["options"][1].as<String>();
    data.dimension_names = doc["dimension_names"];
    data.dimension_ids = doc["dimension_ids"];
    data.latest_values = doc["latest_values"];
    data.view_latest_values = doc["view_latest_values"];
    data.dimensions = doc["dimensions"];
    data.points = doc["points"];
    data.format = doc["format"].as<String>();
    data.result = doc["result"];
    data.min = doc["min"];
    data.max = doc["max"];
    return true;
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
            int readCount = 0;
            
            // 🔵 新增：分块传输解码
            if (isChunked) {
                while (ctx.client->available()) {
                    // 读取 chunk 大小（十六进制行）
                    String line = ctx.client->readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) continue;          // 忽略空行
                    unsigned long chunkSize = strtoul(line.c_str(), NULL, 16);
                    if (chunkSize == 0) break;                 // 终止块 0\r\n
                    
                    // 读取 chunk 数据
                    char* chunkData = new char[chunkSize + 1];
                    ctx.client->readBytes(chunkData, chunkSize);
                    chunkData[chunkSize] = '\0';
                    ctx.bodyBuffer += chunkData;
                    delete[] chunkData;
                    
                    // 跳过 chunk 末尾的 \r\n
                    if (ctx.client->available()) ctx.client->read();
                    if (ctx.client->available()) ctx.client->read();
                    ctx.lastActionTime = now;
                    yield();
                }
                // chunked 解析完成，尝试解析 JSON
                if (parseBatchNetDataResponse(ctx.bodyBuffer)) {
                    ctx.success = true;
                } else {
                    ctx.success = false;
                }
                ctx.state = HTTP_COMPLETED;
            }
            // 🔵 有 Content-Length 的常规读取
            else if (ctx.contentLength > 0) {
                while (ctx.client->available() && ctx.bodyRead < ctx.contentLength) {
                    int toRead = min(ctx.contentLength - ctx.bodyRead, 256); // 一次最多读 256 字节
                    char buf[256];
                    int len = ctx.client->readBytes(buf, toRead);
                    if (len > 0) {
                        ctx.bodyBuffer.concat(buf, len);
                        ctx.bodyRead += len;
                        ctx.lastActionTime = now;
                    }
                    yield(); // 每个大块后 yield 一次即可
                }
                if (ctx.bodyRead >= ctx.contentLength) {
                    // 读取完毕，解析
                    if (ctx.chartID.indexOf(',') != -1) {
                        if (parseBatchNetDataResponse(ctx.bodyBuffer)) {
                            ctx.success = true;
                        } else {
                            ctx.success = false;
                        }
                    } else {
                        if (parseNetDataResponseFromString(ctx.bodyBuffer, *ctx.resultData)) {
                            ctx.success = true;
                        } else {
                            ctx.success = false;
                        }
                    }
                    ctx.state = HTTP_COMPLETED;
                } else if (now - ctx.lastActionTime > TIMEOUT_MS) {
                    ctx.state = HTTP_ERROR;
                }
            }
            // 🔵 无长度且无分块（极少情况），保底：连接关闭即认为完成
            else {
                while (ctx.client->available()) {
                    char c = ctx.client->read();
                    ctx.bodyBuffer += c;
                    ctx.bodyRead++;
                    ctx.lastActionTime = now;
                    // if (++readCount >= 128) {
                    //     yield();
                    //     readCount = 0;
                    // }
                }
                if (!ctx.client->connected() && ctx.bodyBuffer.length() > 0) {
                    if (ctx.chartID.indexOf(',') != -1) {
                        if (parseBatchNetDataResponse(ctx.bodyBuffer)) {
                            ctx.success = true;
                        } else {
                            ctx.success = false;
                        }
                    } else {
                        if (parseNetDataResponseFromString(ctx.bodyBuffer, *ctx.resultData)) {
                            ctx.success = true;
                        } else {
                            ctx.success = false;
                        }
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
        // 请求完成，更新最后使用时间
        markNetdataRequestDone();
        return true;
    }
    return false;
}

// ---------- 启动异步数据请求 ----------
bool startAsyncNetDataRequest(const String& chartID, NetChartData& data, const String& dimFilter) {
    // 检查是否有请求正在进行
    if (httpCtx.state != HTTP_IDLE && httpCtx.state != HTTP_COMPLETED && httpCtx.state != HTTP_ERROR) {
        return false; // 上一请求未结束
    }
    
    // 确保连接可用
    if (!ensureNetdataConnection()) {
        #ifdef DEBUG_ENABLED_0
        Serial.println("Failed to establish NetData connection");
        #endif
        return false;
    }
    
    // 重置上下文
    httpCtx = AsyncHttpContext();
    httpCtx.chartID = chartID;
    httpCtx.dimensionFilter = dimFilter;
    httpCtx.resultData = &data;
    httpCtx.state = HTTP_CONNECTING;
    httpCtx.lastActionTime = millis();
    httpCtx.client = &netdataClient;  // 指向全局 client
    
    // 构建请求字符串
    String reqRes = "/api/v1/data?chart=" + chartID + 
                    "&format=json&points=1&group=average&gtime=0&options=s%7Cjsonwrap%7Cnonzero&after=-2";
    if (dimFilter.length() > 0) reqRes += "&dimensions=" + dimFilter;
    httpCtx.httpRequest = "GET " + reqRes + " HTTP/1.1\r\n" + 
                          "Host: " + String(NETDATA_SERVER_IP) + "\r\n" + 
                          "Connection: keep-alive\r\n" +
                          "User-Agent: RM\r\n" +
                          "Accept: application/json\r\n\r\n";
    return true;
}

#endif
