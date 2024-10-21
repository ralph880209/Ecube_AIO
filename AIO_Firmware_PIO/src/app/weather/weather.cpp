#include "weather.h"
#include "weather_gui.h"
#include "ESP32Time.h"
#include "sys/app_controller.h"
#include "network.h"
#include "common.h"
#include "ArduinoJson.h"
#include <esp32-hal-timer.h>
#include <map>

#define WEATHER_APP_NAME "Weather"
//#define WEATHER_NOW_API "https://www.yiketianqi.com/free/day?appid=%s&appsecret=%s&unescape=1&city=%s"
// v1.yiketianqi.com/api?unescape=1&version=v61
#define WEATHER_NOW_API_UPDATE "https://%s%s?Authorization=%s&locationName=%s&elementName=PoP,MinT,MaxT"
// opendata.cwa.gov.tw/api/v1/rest/datastore/F-C0032-001?Authorization=CWA-58F34622-97A9-4E93-AF8D-7AAA2E805427&locationName=臺中市
#define WEATHER_DALIY_API "https://%sO-A0003-001?Authorization=%s&StationName=%s&WeatherElement=Weather,AirTemperature,RelativeHumidity&GeoInfo=none"
#define TIME_API "https://worldtimeapi.org/api/timezone/Asia/Taipei"
#define WEATHER_PAGE_SIZE 2
#define UPDATE_WEATHER 0x01       // 更新天气
#define UPDATE_DALIY_WEATHER 0x02 // 更新每天天气
#define UPDATE_TIME 0x04          // 更新时间

// 天气的持久化配置
#define WEATHER_CONFIG_PATH "/weather_218.cfg"
struct WT_Config
{
    String tianqi_url;                   // tianqiapi 的url
    String tianqi_appid;                 // tianqiapi 的 appid
    String tianqi_appsecret;             // tianqiapi 的 appsecret
    String tianqi_addr;                  // tianqiapi 的地址（填中文）
    unsigned long weatherUpdataInterval; // 天气更新的时间间隔(s)
    unsigned long timeUpdataInterval;    // 日期时钟更新的时间间隔(s)
};

static void write_config(WT_Config *cfg)
{
    char tmp[128];
    // 将配置数据保存在文件中（持久化）
    String w_data;
    w_data = w_data + cfg->tianqi_url + "\n";
    w_data = w_data + cfg->tianqi_appid + "\n";
    w_data = w_data + cfg->tianqi_appsecret + "\n";
    w_data = w_data + cfg->tianqi_addr + "\n";
    memset(tmp, 0, 16);
    snprintf(tmp, 16, "%lu\n", cfg->weatherUpdataInterval);
    w_data += tmp;
    memset(tmp, 0, 16);
    snprintf(tmp, 16, "%lu\n", cfg->timeUpdataInterval);
    w_data += tmp;
    g_flashCfg.writeFile(WEATHER_CONFIG_PATH, w_data.c_str());
}

static void read_config(WT_Config *cfg)
{
    // 如果有需要持久化配置文件 可以调用此函数将数据存在flash中
    // 配置文件名最好以APP名为开头 以".cfg"结尾，以免多个APP读取混乱
    char info[128] = {0};
    uint16_t size = g_flashCfg.readFile(WEATHER_CONFIG_PATH, (uint8_t *)info);
    info[size] = 0;
    if (size == 0)
    {
        // 默认值
        cfg->tianqi_url = "opendata.cwa.gov.tw/api/v1/rest/datastore/";
        cfg->tianqi_appid = "F-C0032-001";
        cfg->tianqi_addr = "臺中市";
        cfg->weatherUpdataInterval = 600000; // 天气更新的时间间隔900000(900s)
        cfg->timeUpdataInterval = 600000;    // 日期时钟更新的时间间隔900000(900s)
        write_config(cfg);
    }
    else
    {
        // 解析数据
        char *param[6] = {0};
        analyseParam(info, 6, param);
        cfg->tianqi_url = param[0];
        cfg->tianqi_appid = param[1];
        cfg->tianqi_appsecret = param[2];
        cfg->tianqi_addr = param[3];
        cfg->weatherUpdataInterval = atol(param[4]);
        cfg->timeUpdataInterval = atol(param[5]);
    }
}

struct WeatherAppRunData
{
    unsigned long preWeatherMillis; // 上一回更新天气时的毫秒数
    unsigned long preTimeMillis;    // 更新时间计数器
    long long preNetTimestamp;      // 上一次的网络时间戳
    long long errorNetTimestamp;    // 网络到显示过程中的时间误差
    long long preLocalTimestamp;    // 上一次的本地机器时间戳
    unsigned int coactusUpdateFlag; // 强制更新标志
    int clock_page;
    unsigned int update_type; // 更新类型的标志位

    BaseType_t xReturned_task_task_update; // 更新数据的异步任务
    TaskHandle_t xHandle_task_task_update; // 更新数据的异步任务

    ESP32Time g_rtc; // 用于时间解码
    Weather wea;     // 保存天气状况
};

static WT_Config cfg_data;
static WeatherAppRunData *run_data = NULL;

enum wea_event_Id
{
    UPDATE_NOW,
    UPDATE_NTP,
    UPDATE_DAILY
};

std::map<String, int> weatherMap = {{"qing", 0}, {"yin", 1}, {"yu", 2}, {"yun", 3}, {"bingbao", 4}, {"wu", 5}, {"shachen", 6}, {"lei", 7}, {"xue", 8}};//天氣現象對應圖片

static void task_update(void *parameter); 

static int windLevelAnalyse(String str)
{
    int ret = 0;
    for (char ch : str)
    {
        if (ch >= '0' && ch <= '9')
        {
            ret = ret * 10 + (ch - '0');
        }
    }
    return ret;
}

static void get_weather(void)
{
    if (WL_CONNECTED != WiFi.status())
        return;

    HTTPClient http;
    http.setTimeout(1000);
    char api[256] = {0};
    snprintf(api, 256, WEATHER_NOW_API_UPDATE,
             cfg_data.tianqi_url.c_str(),
             cfg_data.tianqi_appid.c_str(),
             cfg_data.tianqi_appsecret.c_str(),
             cfg_data.tianqi_addr.c_str());
    Serial.print("API = ");
    Serial.println(api);
    http.begin(api);

    int httpCode = http.GET();
    if (httpCode > 0)
    {
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
            String payload = http.getString();
            Serial.println(payload);

            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, payload);

            if (error)
            {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                return;
            }

            JsonObject root = doc.as<JsonObject>();
            JsonArray locations = root["records"]["location"].as<JsonArray>();  //利用Jsonarry取出location的資料

            for (JsonObject location : locations)
            {
                strcpy(run_data->wea.cityname, location["locationName"].as<String>().c_str());//取出城市名
                JsonArray weatherElements = location["weatherElement"].as<JsonArray>();//取出weatherElement的資料

                int maxTemp = INT_MIN;
                int minTemp = INT_MAX;
                int pop = -1;

                for (JsonObject weatherElement : weatherElements)
                {
                    String elementName = weatherElement["elementName"].as<String>();

                    if (elementName == "MaxT")  //最高溫度
                    {
                        JsonArray times = weatherElement["time"].as<JsonArray>();
                        for (JsonObject time : times)
                        {
                            JsonObject parameter = time["parameter"].as<JsonObject>();
                            int temp = parameter["parameterName"].as<int>();
                            if (temp > maxTemp)
                            {
                                maxTemp = temp;
                            }
                        }
                    }
                    else if (elementName == "MinT") //最低溫度
                    {
                        JsonArray times = weatherElement["time"].as<JsonArray>();
                        for (JsonObject time : times)
                        {
                            JsonObject parameter = time["parameter"].as<JsonObject>();
                            int temp = parameter["parameterName"].as<int>();
                            if (temp < minTemp)
                            {
                                minTemp = temp;
                            }
                        }
                    }
                    else if (elementName == "PoP")  //降雨機率
                    {
                        JsonArray times = weatherElement["time"].as<JsonArray>();
                        for (JsonObject time : times)
                        {
                            JsonObject parameter = time["parameter"].as<JsonObject>();
                            int poptemp = parameter["parameterName"].as<int>();
                            if (poptemp > pop)
                            {
                                pop = poptemp;
                            }
                        }
                    }
                }

                run_data->wea.maxTemp = maxTemp;
                run_data->wea.minTemp = minTemp;
                run_data->wea.pop = pop; //將取出的資料存入weather結構中

                Serial.print("City Name: ");
                Serial.println(run_data->wea.cityname);
                Serial.print("Max Temperature: ");
                Serial.println(run_data->wea.maxTemp);
                Serial.print("Min Temperature: ");
                Serial.println(run_data->wea.minTemp);
                Serial.print("Probability of Precipitation: ");
                Serial.println(run_data->wea.pop);

                break;
            }
        }
    }
    else
    {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
}


static long long get_timestamp()
{
    // 使用本地的机器时钟
    run_data->preNetTimestamp = run_data->preNetTimestamp + (GET_SYS_MILLIS() - run_data->preLocalTimestamp);
    run_data->preLocalTimestamp = GET_SYS_MILLIS();
    return run_data->preNetTimestamp;
}

static long long get_timestamp(String url)
{
    if (WL_CONNECTED != WiFi.status())
        return 0;

    String time = "";
    HTTPClient http;
    http.setTimeout(1000);
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode > 0)
    {
        if (httpCode == HTTP_CODE_OK)
        {
            String payload = http.getString();
            Serial.println(payload);
    int time_index = payload.indexOf("unixtime") + 10;  // 查找unixtime的位置
    int time_end_index = payload.indexOf(",", time_index); //  查找逗号的位置
    if (time_end_index == -1) {
    time_end_index = payload.indexOf("}", time_index); //如果沒有逗號，找到右括號的位置
    }
    time = payload.substring(time_index, time_end_index);
    delay(100);
            Serial.println(time);
            // 以网络时间戳为准
            run_data->preNetTimestamp = atoll(time.c_str())*1000 + run_data->errorNetTimestamp + TIMEZERO_OFFSIZE;
            run_data->preLocalTimestamp = GET_SYS_MILLIS();
        }
    }
    else
    {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        // 得不到网络时间戳时
        run_data->preNetTimestamp = run_data->preNetTimestamp + (GET_SYS_MILLIS() - run_data->preLocalTimestamp);
        run_data->preLocalTimestamp = GET_SYS_MILLIS();
    }
    http.end();

    return run_data->preNetTimestamp;
}

static void get_daliyWeather(short maxT[], short minT[])
{
    if (WL_CONNECTED != WiFi.status())
        return;

    HTTPClient http;
    http.setTimeout(1000);
    char api[256] = {0};
    
    // 提取 cfg_data.tianqi_addr 前两个字符
    String addr = cfg_data.tianqi_addr.c_str();
    String shortAddr;
    
    // 检查字符串长度是否足够
    if (addr.length() >= 6)  // 假設地址長度至少為6
    {
        shortAddr = addr.substring(0, 6);  //取前兩個字元
    }
    else
    {
        shortAddr = addr;  // 字串長度不足，直接取全部
    }

    //選擇選用的地區及對應的觀測站
    if (shortAddr == "桃園") 
    {
        shortAddr = "新屋";
    }
    else if (shortAddr == "苗栗")
    {
        shortAddr = "後龍";
    }
    else if (shortAddr == "彰化")
    {
        shortAddr = "田中";
    }
    else if (shortAddr == "南投")
    {
        shortAddr = "玉山";
    }
    else if (shortAddr == "雲林")
    {
        shortAddr = "草嶺";
    }
    else if (shortAddr == "連江")
    {
        shortAddr = "馬祖";
    }
        else if (shortAddr == "屏東")
    {
        shortAddr = "恆春";
    }

    snprintf(api, 256, WEATHER_DALIY_API,
             cfg_data.tianqi_url.c_str(),
             cfg_data.tianqi_appsecret.c_str(),
             shortAddr.c_str());
    
    Serial.print("API = ");
    Serial.println(api);
    http.begin(api);

    int httpCode = http.GET();
    if (httpCode > 0)
    {
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
            String payload = http.getString();
            Serial.println(payload);
            DynamicJsonDocument doc(4096); // 增大文档大小以适应更多数据
            deserializeJson(doc, payload);
            JsonObject root = doc.as<JsonObject>();

            // 获取 humidity 和 temperature
            int humidity = root["records"]["Station"][0]["WeatherElement"]["RelativeHumidity"].as<int>();
            float temperature = root["records"]["Station"][0]["WeatherElement"]["AirTemperature"].as<float>();

            run_data->wea.humidity = humidity;
            run_data->wea.temperature = static_cast<int>(temperature + 0.5f);

            // 打印 humidity 和 temperature
            Serial.print("Relative Humidity: ");
            Serial.println(run_data->wea.humidity);
            Serial.print("Air Temperature: ");
            Serial.println(run_data->wea.temperature);

            // 获取并处理 Weather
            String weather = root["records"]["Station"][0]["WeatherElement"]["Weather"].as<String>();
            if (weather.indexOf("晴") != -1) 
            {
                run_data->wea.weather_code = 0;
                run_data->wea.airQulity = 0;
            }
            else if (weather.indexOf("雨") != -1 || weather.indexOf("雷") != -1) 
            {
                run_data->wea.weather_code = 7;
                run_data->wea.airQulity = 7;
            }
            else if (weather.indexOf("陰") != -1) 
            {
                run_data->wea.weather_code = 1;
                run_data->wea.airQulity = 1;
            }
            else if (weather.indexOf("多雲") != -1) 
            {
                run_data->wea.weather_code = 3;
                run_data->wea.airQulity = 3;
            }

            // 打印 Weather
            Serial.print("Weather: ");
            Serial.println(run_data->wea.weather_code);

            JsonArray data = root["records"]["Station"];
            for (int gDW_i = 0; gDW_i < 7; ++gDW_i)
            {
                maxT[gDW_i] = data[gDW_i]["WeatherElement"]["DailyExtreme"]["DailyHigh"]["TemperatureInfo"]["AirTemperature"].as<int>();
                minT[gDW_i] = data[gDW_i]["WeatherElement"]["DailyExtreme"]["DailyLow"]["TemperatureInfo"]["AirTemperature"].as<int>();
            }
        }
    }
    else
    {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
}



static void UpdateTime_RTC(long long timestamp)
{
    struct TimeStr t;
    run_data->g_rtc.setTime(timestamp / 1000);
    t.month = run_data->g_rtc.getMonth() + 1;
    t.day = run_data->g_rtc.getDay();
    t.hour = run_data->g_rtc.getHour(true);
    t.minute = run_data->g_rtc.getMinute();
    t.second = run_data->g_rtc.getSecond();
    t.weekday = run_data->g_rtc.getDayofWeek();
    // Serial.printf("time : %d-%d-%d\n",t.hour, t.minute, t.second);
    display_time(t, LV_SCR_LOAD_ANIM_NONE);
}

static int weather_init(AppController *sys)
{
    tft->setSwapBytes(true);
    weather_gui_init();
    // 获取配置信息
    read_config(&cfg_data);

    // 初始化运行时参数
    run_data = (WeatherAppRunData *)calloc(1, sizeof(WeatherAppRunData));
    memset((char *)&run_data->wea, 0, sizeof(Weather));
    run_data->preNetTimestamp = 1577808000000; // 上一次的网络时间戳 初始化为2020-01-01 00:00:00
    run_data->errorNetTimestamp = 2;
    run_data->preLocalTimestamp = GET_SYS_MILLIS(); // 上一次的本地机器时间戳
    run_data->clock_page = 0;
    run_data->preWeatherMillis = 0;
    run_data->preTimeMillis = 0;
    // 强制更新
    run_data->coactusUpdateFlag = 0x01;
    run_data->update_type = 0x00; // 表示什么也不需要更新

    // 目前更新数据的任务栈大小5000够用，4000不够用
    // 为了后期迭代新功能 当前设置为8000
    // run_data->xReturned_task_task_update = xTaskCreate(
    //     task_update,                          /*任务函数*/
    //     "Task_update",                        /*带任务名称的字符串*/
    //     8000,                                /*堆栈大小，单位为字节*/
    //     NULL,                                 /*作为任务输入传递的参数*/
    //     1,                                    /*任务的优先级*/
    //     &run_data->xHandle_task_task_update); /*任务句柄*/

    return 0;
}

static void weather_process(AppController *sys,
                            const ImuAction *act_info)
{
    lv_scr_load_anim_t anim_type = LV_SCR_LOAD_ANIM_NONE;

    if (RETURN == act_info->active)
    {
        sys->app_exit();
        return;
    }
    else if (GO_FORWORD == act_info->active)
    {
        // 间接强制更新
        run_data->coactusUpdateFlag = 0x01;
        delay(500); // 以防间接强制更新后，生产很多请求 使显示卡顿
    }
   

    // 界面刷新
    if (run_data->clock_page == 0)
    {
        display_weather(run_data->wea, anim_type);
        if (0x01 == run_data->coactusUpdateFlag || doDelayMillisTime(cfg_data.weatherUpdataInterval, &run_data->preWeatherMillis, false))
        {
            sys->send_to(WEATHER_APP_NAME, CTRL_NAME,
                         APP_MESSAGE_WIFI_CONN, (void *)UPDATE_NOW, NULL);
            sys->send_to(WEATHER_APP_NAME, CTRL_NAME,
                         APP_MESSAGE_WIFI_CONN, (void *)UPDATE_DAILY, NULL);
        }

        if (0x01 == run_data->coactusUpdateFlag || doDelayMillisTime(cfg_data.timeUpdataInterval, &run_data->preTimeMillis, false))
        {
            // 尝试同步网络上的时钟
            sys->send_to(WEATHER_APP_NAME, CTRL_NAME,
                         APP_MESSAGE_WIFI_CONN, (void *)UPDATE_NTP, NULL);
        }
        else if (GET_SYS_MILLIS() - run_data->preLocalTimestamp > 400)
        {
            UpdateTime_RTC(get_timestamp());
        }
        run_data->coactusUpdateFlag = 0x00; // 取消强制更新标志
        display_space();
        delay(30);
    }
    else if (run_data->clock_page == 1)
    {
        // 仅在切换界面时获取一次未来天气
        display_curve(run_data->wea.daily_max, run_data->wea.daily_min, anim_type);
        delay(300);
    }
}

static void weather_background_task(AppController *sys,
                                    const ImuAction *act_info)
{
    // 本函数为后台任务，主控制器会间隔一分钟调用此函数
    // 本函数尽量只调用"常驻数据",其他变量可能会因为生命周期的缘故已经释放
}

static int weather_exit_callback(void *param)
{
    weather_gui_del();

    // 查杀异步任务
    if (run_data->xReturned_task_task_update == pdPASS)
    {
        vTaskDelete(run_data->xHandle_task_task_update);
    }

    // 释放运行数据
    if (NULL != run_data)
    {
        free(run_data);
        run_data = NULL;
    }
    return 0;
}

// static void task_update(void *parameter)
// {
//     // 数据更新任务
//     while (1)
//     {
//         if (run_data->update_type & UPDATE_WEATHER)
//         {
//             get_weather();
//             if (run_data->clock_page == 0)
//             {
//                 display_weather(run_data->wea, LV_SCR_LOAD_ANIM_NONE);
//             }
//             run_data->update_type &= (~UPDATE_WEATHER);
//         }
//         if (run_data->update_type & UPDATE_TIME)
//         {
//             long long timestamp = get_timestamp(TIME_API); // nowapi时间API
//             if (run_data->clock_page == 0)
//             {
//                 UpdateTime_RTC(timestamp);
//             }
//             run_data->update_type &= (~UPDATE_TIME);
//         }
//         if (run_data->update_type & UPDATE_DALIY_WEATHER)
//         {
//             get_daliyWeather(run_data->wea.daily_max, run_data->wea.daily_min);
//             if (run_data->clock_page == 1)
//             {
//                 display_curve(run_data->wea.daily_max, run_data->wea.daily_min, LV_SCR_LOAD_ANIM_NONE);
//             }
//             run_data->update_type &= (~UPDATE_DALIY_WEATHER);
//         }
//         vTaskDelay(2000 / portTICK_PERIOD_MS);
//     }
// }

static void weather_message_handle(const char *from, const char *to,
                                   APP_MESSAGE_TYPE type, void *message,
                                   void *ext_info)
{
    switch (type)
    {
    case APP_MESSAGE_WIFI_CONN:
    {
        Serial.println(F("----->weather_event_notification"));
        int event_id = (int)message;
        switch (event_id)
        {
        case UPDATE_NOW:
        {
            Serial.print(F("weather update.\n"));
            run_data->update_type |= UPDATE_WEATHER;

            get_weather();
            if (run_data->clock_page == 0)
            {
                display_weather(run_data->wea, LV_SCR_LOAD_ANIM_NONE);
            }
        };
        break;
        case UPDATE_NTP:
        {
            Serial.print(F("ntp update.\n"));
            run_data->update_type |= UPDATE_TIME;

            long long timestamp = get_timestamp(TIME_API); // nowapi时间API
            if (run_data->clock_page == 0)
            {
                UpdateTime_RTC(timestamp);
            }
        };
        break;
        case UPDATE_DAILY:
        {
            Serial.print(F("daliy update.\n"));
            run_data->update_type |= UPDATE_DALIY_WEATHER;

            get_daliyWeather(run_data->wea.daily_max, run_data->wea.daily_min);
            if (run_data->clock_page == 1)
            {
                display_curve(run_data->wea.daily_max, run_data->wea.daily_min, LV_SCR_LOAD_ANIM_NONE);
            }
        };
        break;
        default:
            break;
        }
    }
    break;
    case APP_MESSAGE_GET_PARAM:
    {
        char *param_key = (char *)message;
        if (!strcmp(param_key, "tianqi_url"))
        {
            snprintf((char *)ext_info, 128, "%s", cfg_data.tianqi_url.c_str());
        }
        else if (!strcmp(param_key, "tianqi_appid"))
        {
            snprintf((char *)ext_info, 32, "%s", cfg_data.tianqi_appid.c_str());
        }
        else if (!strcmp(param_key, "tianqi_appsecret"))
        {
            snprintf((char *)ext_info, 64, "%s", cfg_data.tianqi_appsecret.c_str());
        }
        else if (!strcmp(param_key, "tianqi_addr"))
        {
            snprintf((char *)ext_info, 256, "%s", cfg_data.tianqi_addr.c_str());
        }
        else if (!strcmp(param_key, "weatherUpdataInterval"))
        {
            snprintf((char *)ext_info, 32, "%lu", cfg_data.weatherUpdataInterval);
        }
        else if (!strcmp(param_key, "timeUpdataInterval"))
        {
            snprintf((char *)ext_info, 32, "%lu", cfg_data.timeUpdataInterval);
        }
        else
        {
            snprintf((char *)ext_info, 32, "%s", "NULL");
        }
    }
    break;
    case APP_MESSAGE_SET_PARAM:
    {
        char *param_key = (char *)message;
        char *param_val = (char *)ext_info;
        if (!strcmp(param_key, "tianqi_url"))
        {
            cfg_data.tianqi_url = param_val;
        }
        else if (!strcmp(param_key, "tianqi_appid"))
        {
            cfg_data.tianqi_appid = param_val;
        }
        else if (!strcmp(param_key, "tianqi_appsecret"))
        {
            cfg_data.tianqi_appsecret = param_val;
        }
        else if (!strcmp(param_key, "tianqi_addr"))
        {
            cfg_data.tianqi_addr = param_val;
        }
        else if (!strcmp(param_key, "weatherUpdataInterval"))
        {
            cfg_data.weatherUpdataInterval = atol(param_val);
        }
        else if (!strcmp(param_key, "timeUpdataInterval"))
        {
            cfg_data.timeUpdataInterval = atol(param_val);
        }
    }
    break;
    case APP_MESSAGE_READ_CFG:
    {
        read_config(&cfg_data);
    }
    break;
    case APP_MESSAGE_WRITE_CFG:
    {
        write_config(&cfg_data);
    }
    break;
    default:
        break;
    }
}

APP_OBJ weather_app = {WEATHER_APP_NAME, &app_weather, "",
                       weather_init, weather_process, weather_background_task,
                       weather_exit_callback, weather_message_handle};
