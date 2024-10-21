
// 參考代碼 https://github.com/G6EJD/ESP32-8266-File-Upload

#include "network.h"
#include "common.h"
#include "server.h"
#include "web_setting.h"
#include "app/app_conf.h"
#include "FS.h"
#include "HardwareSerial.h"
#include <esp32-hal.h>

boolean sd_present = true;
String webpage = "";
String webpage_header = "";
String webpage_footer = "";

void Send_HTML(const String &content)
{
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server.send(200, "text/html", "");
    server.sendContent(webpage_header);

    server.sendContent(content);
    server.sendContent(webpage_footer);

    server.sendContent("");
    server.client().stop(); // Stop is needed because no content length was sent
}

String file_size(int bytes)
{
    String fsize = "";
    if (bytes < 1024)
        fsize = String(bytes) + " B";
    else if (bytes < (1024 * 1024))
        fsize = String(bytes / 1024.0, 3) + " KB";
    else if (bytes < (1024 * 1024 * 1024))
        fsize = String(bytes / 1024.0 / 1024.0, 3) + " MB";
    else
        fsize = String(bytes / 1024.0 / 1024.0 / 1024.0, 3) + " GB";
    return fsize;
}

#define SETING_CSS ".input {display: block;margin-top: 10px;}"                                          \
                   ".input span {width: 300px;float: left;float: left;height: 30px;line-height: 36px;}" \
                   ".input select {height: 35px;}"                                                      \
                   ".input input {height: 30px;width: 200px;}"                                          \
                   ".input .radio {height: 30px;width: 50px;}"                                          \
                   ".hidden {display: none;}"                                                           \
                   ".btn {width: 120px;height: 35px;background-color: #000000;border: 0px;color: #ffffff;margin-top: 15px;margin-left: auto;}" // margin-left: 100px;

#define SYS_SETTING "<form method=\"GET\" action=\"saveSysConf\">"                                                                                                                                                                                                      \
                    "<label class=\"input\"><span>WiFi 名稱(2.4G)</span><input type=\"text\"name=\"ssid_0\"value=\"%s\"></label>"                                                                                                                                     \
                    "<label class=\"input\"><span>WiFi 密碼</span><input type=\"text\"name=\"password_0\"value=\"%s\"></label>"                                                                                                                                     \
                    "<label class=\"input\"><span>功耗控制（0低發熱 1性能優先）</span><input type=\"text\"name=\"power_mode\"value=\"%s\"></label>"                                                                                                        \
                    "<label class=\"input\"><span>螢幕亮度 (值為1~100)</span><input type=\"text\"name=\"backLight\"value=\"%s\"></label>"                                                                                                                         \
                    "<label class=\"input hidden\"><span>螢幕方向 (0~5可選)</span><input type=\"text\"name=\"rotation\"value=\"%s\"></label>"                                                                                                                            \
                    "<label class=\"input hidden\"><span>操作方向（0~15可選）</span><input type=\"text\"name=\"mpu_order\"value=\"%s\"></label>"                                                                                                                       \
                    "<label class=\"input\"><span>MPU6050自動校準</span><input class=\"radio\" type=\"radio\" value=\"0\" name=\"auto_calibration_mpu\" %s>關閉<input class=\"radio\" type=\"radio\" value=\"1\" name=\"auto_calibration_mpu\" %s>開啟</label>" \
                    "<label class=\"input\"><span>開機自啟的APP名字</span><input type=\"text\"name=\"auto_start_app\"value=\"%s\"></label>"                                                                                                                      \
                    "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define RGB_SETTING "<form method=\"GET\" action=\"saveRgbConf\">"                                                                                             \
                    "<label class=\"input\"><span>RGB最低亮度（0.00~0.99可選）</span><input type=\"text\"name=\"min_brightness\"value=\"%s\"></label>" \
                    "<label class=\"input\"><span>RGB最高亮度（0.00~0.99可選）</span><input type=\"text\"name=\"max_brightness\"value=\"%s\"></label>" \
                    "<label class=\"input\"><span>RGB漸變時間（整數毫秒值）</span><input type=\"text\"name=\"time\"value=\"%s\"></label>"           \
                    "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define WEATHER_SETTING "<form method=\"GET\" action=\"saveWeatherConf\">"                                                                  \
                        "<label class=\"input\"><span>來源網址</span><input type=\"text\" name=\"tianqi_url\" value=\"%s\" readonly></label>"     \
                        "<label class=\"input\"><span>類別編號</span><input type=\"text\" name=\"tianqi_appid\" value=\"%s\" readonly></label>" \
                        "<label class=\"input\"><span>授權碼</span><input type=\"text\" name=\"tianqi_appsecret\" value=\"%s\"></label>" \
                        "<label class=\"input\"><span>地區</span><select name=\"tianqi_addr\">"                            \
                        "<option value=\"宜蘭縣\" %s>宜蘭縣</option>"                                                                     \
                        "<option value=\"花蓮縣\" %s>花蓮縣</option>"                                                                     \
                        "<option value=\"臺東縣\" %s>臺東縣</option>"                                                                     \
                        "<option value=\"澎湖縣\" %s>澎湖縣</option>"                                                                     \
                        "<option value=\"金門縣\" %s>金門縣</option>"                                                                     \
                        "<option value=\"連江縣\" %s>連江縣</option>"                                                                     \
                        "<option value=\"臺北市\" %s>臺北市</option>"                                                                     \
                        "<option value=\"新北市\" %s>新北市</option>"                                                                     \
                        "<option value=\"桃園市\" %s>桃園市</option>"                                                                     \
                        "<option value=\"臺中市\" %s>臺中市</option>"                                                                     \
                        "<option value=\"臺南市\" %s>臺南市</option>"                                                                     \
                        "<option value=\"高雄市\" %s>高雄市</option>"                                                                     \
                        "<option value=\"基隆市\" %s>基隆市</option>"                                                                     \
                        "<option value=\"新竹縣\" %s>新竹縣</option>"                                                                     \
                        "<option value=\"新竹市\" %s>新竹市</option>"                                                                     \
                        "<option value=\"苗栗縣\" %s>苗栗縣</option>"                                                                     \
                        "<option value=\"彰化縣\" %s>彰化縣</option>"                                                                     \
                        "<option value=\"南投縣\" %s>南投縣</option>"                                                                     \
                        "<option value=\"雲林縣\" %s>雲林縣</option>"                                                                     \
                        "<option value=\"嘉義縣\" %s>嘉義縣</option>"                                                                     \
                        "<option value=\"嘉義市\" %s>嘉義市</option>"                                                                     \
                        "<option value=\"屏東縣\" %s>屏東縣</option>"                                                                     \
                        "</select></label>"                                                                                               \
                        "<label class=\"input\"><span>天氣更新週期（毫秒）</span><input type=\"text\" name=\"weatherUpdataInterval\" value=\"%s\"></label>" \
                        "<label class=\"input\"><span>日期更新週期（毫秒）</span><input type=\"text\" name=\"timeUpdataInterval\" value=\"%s\"></label>"  \
                        "<input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define WEATHER_OLD_SETTING "<form method=\"GET\" action=\"saveWeatherOldConf\">"                                                                                       \
                            "<label class=\"input\"><span>知心天氣 城市名（拼音）</span><input type=\"text\"name=\"cityname\"value=\"%s\"></label>"          \
                            "<label class=\"input\"><span>City Language(zh-Hans)</span><input type=\"text\"name=\"language\"value=\"%s\"></label>"                      \
                            "<label class=\"input\"><span>Weather Key</span><input type=\"text\"name=\"weather_key\"value=\"%s\"></label>"                              \
                            "<label class=\"input\"><span>天氣更新週期（毫秒）</span><input type=\"text\"name=\"weatherUpdataInterval\"value=\"%s\"></label>" \
                            "<label class=\"input\"><span>日期更新週期（毫秒）</span><input type=\"text\"name=\"timeUpdataInterval\"value=\"%s\"></label>"    \
                            "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define BILIBILI_SETTING "<form method=\"GET\" action=\"saveBiliConf\">"                                                                                      \
                         "<label class=\"input\"><span>Bili UID</span><input type=\"text\"name=\"bili_uid\"value=\"%s\"></label>"                             \
                         "<label class=\"input\"><span>資料更新週期（毫秒）</span><input type=\"text\"name=\"updataInterval\"value=\"%s\"></label>" \
                         "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define STOCK_SETTING "<form method=\"GET\" action=\"saveStockConf\">"                                                                                          \
                      "<label class=\"input\"><span>股票代碼,例如：sz000001或sh601126</span><input type=\"text\"name=\"stock_id\"value=\"%s\"></label>" \
                      "<label class=\"input\"><span>資料更新週期（毫秒）</span><input type=\"text\"name=\"updataInterval\"value=\"%s\"></label>"      \
                      "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define PICTURE_SETTING "<form method=\"GET\" action=\"savePictureConf\">"                                                                                         \
                        "<label class=\"input\"><span>自動切換時間間隔（毫秒）</span><input type=\"text\"name=\"switchInterval\"value=\"%s\"></label>" \
                        "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define MEDIA_SETTING "<form method=\"GET\" action=\"saveMediaConf\">"                                                                                             \
                      "<label class=\"input\"><span>自動切換（0不切換 1自動切換）</span><input type=\"text\"name=\"switchFlag\"value=\"%s\"></label>" \
                      "<label class=\"input\"><span>功耗控制（0低發熱 1性能優先）</span><input type=\"text\"name=\"powerFlag\"value=\"%s\"></label>"  \
                      "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define SCREEN_SETTING "<form method=\"GET\" action=\"saveScreenConf\">"                                                                                           \
                       "<label class=\"input\"><span>功耗控制（0低發熱 1性能優先）</span><input type=\"text\"name=\"powerFlag\"value=\"%s\"></label>" \
                       "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define HEARTBEAT_SETTING "<form method=\"GET\" action=\"saveHeartbeatConf\">"                                                                            \
                          "<label class=\"input\"><span>Role(0:heart,1:beat)</span><input type=\"text\"name=\"role\"value=\"%s\"></label>"                \
                          "<label class=\"input\"><span>MQTT ClientID(推薦QQ號)</span><input type=\"text\"name=\"mqtt_client_id\"value=\"%s\"></label>"                    \  
                          "<label class=\"input\"><span>MQTT SubTopic(推薦對方QQ號)</span><input type=\"text\"name=\"mqtt_subtopic\"value=\"%s\"></label>"                    \  
                        "<label class=\"input\"><span>MQTT ServerIp</span><input type=\"text\"name=\"mqtt_server\"value=\"%s\"></label>"                    \  
                        "<label class=\"input\"><span>MQTT 埠號(1883)</span><input type=\"text\"name=\"mqtt_port\"value=\"%s\"></label>"             \
                        "<label class=\"input\"><span>MQTT 服務用戶名(可不填)</span><input type=\"text\"name=\"mqtt_user\"value=\"%s\"></label>"  \
                        "<label class=\"input\"><span>MQTT 服務密碼(可不填)</span><input type=\"text\"name=\"mqtt_password\"value=\"%s\"></label>" \
                        "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define ANNIVERSARY_SETTING "<form method=\"GET\" action=\"saveAnniversaryConf\">"                                                      \
                            "<label class=\"input\"><span>事件1</span><input type=\"text\"name=\"event_name0\"value=\"%s\"></label>"  \
                            "<label class=\"input\"><span>日期1</span><input type=\"text\"name=\"target_date0\"value=\"%s\"></label>" \
                            "<label class=\"input\"><span>事件2</span><input type=\"text\"name=\"event_name1\"value=\"%s\"></label>"  \
                            "<label class=\"input\"><span>日期2</span><input type=\"text\"name=\"target_date1\"value=\"%s\"></label>" \
                            "<label class=\"input\"><span>年份填0為每年固定發生事件</span></label>" \
                            "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

#define REMOTR_SENSOR_SETTING "<form method=\"GET\" action=\"savePCResourceConf\">"                                                                                       \
                              "<label class=\"input\"><span>PC地址</span><input type=\"text\"name=\"pc_ipaddr\"value=\"%s\"></label>"                                   \
                              "<label class=\"input\"><span>感測器資料更新間隔(ms)</span><input type=\"text\"name=\"sensorUpdataInterval\"value=\"%s\"></label>" \
                              "</label><input class=\"btn\" type=\"submit\" name=\"submit\" value=\"保存\"></form>"

void init_page_header()
{
    webpage_header = F("<!DOCTYPE html><html>");
    webpage_header += F("<head>");
    webpage_header += F("<title>WebServer "); // NOTE: 1em = 16px
    webpage_header += F(AIO_VERSION"</title>");
    webpage_header += F("<meta http-equiv='Content-Type' name='viewport' content='user-scalable=yes,initial-scale=1.0,width=device-width; text/html; charset=utf-8' />");
    webpage_header += F("<style>");
    webpage_header += F(SETING_CSS);
    webpage_header += F("body{max-width:65%;margin:0 auto;font-family:arial;font-size:105%;text-align:center;color:blue;background-color:#c7ede9;}");
    webpage_header += F("ul{list-style-type:none;margin:0.1em;padding:0;border-radius:0.375em;overflow:hidden;background-color:#48b3e8;font-size:1em;}");
    webpage_header += F("li{float:left;border-radius:0.375em;border-right:0.06em solid #bbb;}last-child {border-right:none;font-size:85%}");
    webpage_header += F("li a{display: block;border-radius:0.375em;padding:0.44em 0.44em;text-decoration:none;font-size:85%}");
    webpage_header += F("li a:hover{background-color:#EAE3EA;border-radius:0.375em;font-size:85%}");
    webpage_header += F("section {font-size:0.88em;}");
    webpage_header += F("h1{color:white;border-radius:0.5em;font-size:1em;padding:0.2em 0.2em;background:#29a7e6;}");
    webpage_header += F("h2{color:orange;font-size:1.0em;}");
    webpage_header += F("h3{font-size:0.8em;}");
    webpage_header += F("table{font-family:arial,sans-serif;font-size:0.9em;border-collapse:collapse;width:85%;}");
    webpage_header += F("th,td {border:0.06em solid #dddddd;text-align:left;padding:0.3em;border-bottom:0.06em solid #dddddd;}");
    webpage_header += F("tr:nth-child(odd) {background-color:#eeeeee;}");
    webpage_header += F(".rcorners_n {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:20%;color:white;font-size:75%;}");
    webpage_header += F(".rcorners_m {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:50%;color:white;font-size:75%;}");
    webpage_header += F(".rcorners_w {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:70%;color:white;font-size:75%;}");
    webpage_header += F(".column{float:left;width:50%;height:45%;}");
    webpage_header += F(".row:after{content:'';display:table;clear:both;}");
    webpage_header += F("*{box-sizing:border-box;}");
    webpage_header += F("footer{background-color:#48b3e8; text-align:center;padding:0.3em 0.3em;border-radius:0.375em;font-size:60%;}");
    webpage_header += F("button{border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:20%;color:white;font-size:130%;}");
    webpage_header += F(".buttons {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:15%;color:white;font-size:80%;}");
    webpage_header += F(".buttonsm{border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:9%; color:white;font-size:70%;}");
    webpage_header += F(".buttonm {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:15%;color:white;font-size:70%;}");
    webpage_header += F(".buttonw {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:40%;color:white;font-size:70%;}");
    webpage_header += F("a{font-size:75%;}");
    webpage_header += F("p{font-size:75%;}");

    webpage_header += F("</style></head><body>");

    webpage_header += F("<h1>WebServer</h1>");
    webpage_header += F("<ul>");
   // webpage_header += F("<li><a href='/'>Home</a></li>"); // Lower Menu bar command entries
  //  webpage_header += F("<li><a href='/download'>Download</a></li>");
  
 //   webpage_header += F("<li><a href='/delete'>Delete</a></li>");

    webpage_header += F("<li><a href='/sys_setting'>系統設置</a></li>");
    webpage_header += F("<li><a href='/upload'>上傳檔案</a></li>");

    webpage_header += F("<li><a href='/rgb_setting'>RGB設置</a></li>");
#if APP_WEATHER_USE
    webpage_header += F("<li><a href='/weather_setting'>天氣設置</a></li>");
#endif
#if APP_WEATHER_OLD_USE
    webpage_header += F("<li><a href='/weather_old_setting'>舊版天氣</a></li>");
#endif
#if APP_BILIBILI_FANS_USE
    webpage_header += F("<li><a href='/bili_setting'>B站</a></li>");
#endif
#if APP_PICTURE_USE
    webpage_header += F("<li><a href='/picture_setting'>相冊</a></li>");
#endif
#if APP_MEDIA_PLAYER_USE
    webpage_header += F("<li><a href='/media_setting'>媒體播放器</a></li>");
#endif
#if APP_SCREEN_SHARE_USE
    webpage_header += F("<li><a href='/screen_setting'>螢幕分享</a></li>");
#endif
#if APP_HEARTBEAT_USE
    webpage_header += F("<li><a href='/heartbeat_setting'>心跳</a></li>");
#endif
#if APP_ANNIVERSARY_USE
    webpage_header += F("<li><a href='/anniversary_setting'>紀念日</a></li>");
#endif
#if APP_STOCK_MARKET_USE
    webpage_header += F("<li><a href='/stock_setting'>股票行情</a></li>");
#endif
#if APP_PC_RESOURCE_USE
    webpage_header += F("<li><a href='/pc_resource_setting'>PC資源監控</a></li>");
#endif
    webpage_header += F("</ul>");
}

void init_page_footer()
{
    webpage_footer = F("<footer>VERSION: ");
    webpage_footer += F(AIO_VERSION"<footer/>");
    webpage_footer += F("</body></html>");
}

// All supporting functions from here...
void HomePage()
{
    // 指定 target='_blank' 設置新建頁面
    webpage = F("<a href='https://github.com/ClimbSnail/HoloCubic_AIO' target='_blank'><button>Github</button></a>");
    webpage += F("<a href='https://space.bilibili.com/344470052?spm_id_from=333.788.b_765f7570696e666f.1' target='_blank'><button>BiliBili教程</button></a>");
    Send_HTML(webpage);
}

void sys_setting()
{
    char buf[2048];
    char ssid_0[32];
    char password_0[32];
    char power_mode[32];
    char backLight[32];
    char rotation[32];
    char mpu_order[32];
    char min_brightness[32];
    char max_brightness[32];
    char time[32];
    char auto_calibration_mpu[32];
    char auto_start_app[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"ssid_0", ssid_0);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"password_0", password_0);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"power_mode", power_mode);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"backLight", backLight);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"rotation", rotation);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"mpu_order", mpu_order);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"min_brightness", min_brightness);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"max_brightness", max_brightness);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"time", time);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"auto_calibration_mpu", auto_calibration_mpu);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"auto_start_app", auto_start_app);
    SysUtilConfig cfg = app_controller->sys_cfg;
    // 主要為了處理啟停MPU自動校準的單選框
    if (0 == cfg.auto_calibration_mpu)
    {
        sprintf(buf, SYS_SETTING,
                ssid_0, password_0,
                power_mode, backLight, rotation,
                mpu_order, "checked=\"checked\"", "",
                auto_start_app);
    }
    else
    {
        sprintf(buf, SYS_SETTING,
                ssid_0, password_0,
                power_mode, backLight, rotation,
                mpu_order, "", "checked=\"checked\"",
                auto_start_app);
    }
    webpage = buf;
    Send_HTML(webpage);
}

void rgb_setting()
{
    char buf[2048];
    char min_brightness[32];
    char max_brightness[32];
    char time[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"min_brightness", min_brightness);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"max_brightness", max_brightness);
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_GET_PARAM,
                            (void *)"time", time);
    sprintf(buf, RGB_SETTING,
            min_brightness, max_brightness, time);
    webpage = buf;
    Send_HTML(webpage);
}

                                //讀取並傳送至HTML %S
void weather_setting()
{
    char buf[3072];
    char tianqi_url[128] = {0};
    char tianqi_appid[64] = {0};
    char tianqi_appsecret[256] = {0};
    char tianqi_addr[128] = {0};
    char weatherUpdataInterval[32] = {0};
    char timeUpdataInterval[32] = {0};
    char selected[25][10] = {{0}};
    
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_READ_CFG, NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_GET_PARAM, (void *)"tianqi_url", tianqi_url);
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_GET_PARAM, (void *)"tianqi_appid", tianqi_appid);
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_GET_PARAM, (void *)"tianqi_appsecret", tianqi_appsecret);
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_GET_PARAM, (void *)"tianqi_addr", tianqi_addr);
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_GET_PARAM, (void *)"weatherUpdataInterval", weatherUpdataInterval);
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_GET_PARAM, (void *)"timeUpdataInterval", timeUpdataInterval);

    // 確認選擇的城市
    const char* city_list[] = {
        "宜蘭縣", "花蓮縣", "臺東縣", "澎湖縣", "金門縣", "連江縣", "臺北市", "新北市", 
        "桃園市", "臺中市", "臺南市", "高雄市", "基隆市", "新竹縣", "新竹市", "苗栗縣", 
        "彰化縣", "南投縣", "雲林縣", "嘉義縣", "嘉義市", "屏東縣"
    };

    for (int i = 0; i < 22; ++i)
    {
        if (strcmp(tianqi_addr, city_list[i]) == 0)
        {
            strcpy(selected[i], "selected");
        }
    }

    snprintf(buf, sizeof(buf), WEATHER_SETTING, 
             tianqi_url, 
             tianqi_appid,
             tianqi_appsecret, 
             selected[0], selected[1], selected[2], selected[3], selected[4], 
             selected[5], selected[6], selected[7], selected[8], selected[9], 
             selected[10], selected[11], selected[12], selected[13], selected[14], 
             selected[15], selected[16], selected[17], selected[18], selected[19], 
             selected[20], selected[21],
             weatherUpdataInterval, 
             timeUpdataInterval);

    webpage = buf;
    Send_HTML(webpage);
}


void weather_old_setting()
{
    char buf[2048];
    char cityname[32];
    char language[32];
    char weather_key[32];
    char weatherUpdataInterval[32];
    char timeUpdataInterval[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_GET_PARAM,
                            (void *)"cityname", cityname);
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_GET_PARAM,
                            (void *)"language", language);
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_GET_PARAM,
                            (void *)"weather_key", weather_key);
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_GET_PARAM,
                            (void *)"weatherUpdataInterval", weatherUpdataInterval);
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_GET_PARAM,
                            (void *)"timeUpdataInterval", timeUpdataInterval);
    sprintf(buf, WEATHER_OLD_SETTING,
            cityname,
            language,
            weather_key,
            weatherUpdataInterval,
            timeUpdataInterval);
    webpage = buf;
    Send_HTML(webpage);
}

void bili_setting()
{
    char buf[2048];
    char bili_uid[32];
    char updataInterval[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Bili", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Bili", APP_MESSAGE_GET_PARAM,
                            (void *)"bili_uid", bili_uid);
    app_controller->send_to(SERVER_APP_NAME, "Bili", APP_MESSAGE_GET_PARAM,
                            (void *)"updataInterval", updataInterval);
    sprintf(buf, BILIBILI_SETTING, bili_uid, updataInterval);
    webpage = buf;
    Send_HTML(webpage);
}

void stock_setting()
{
    char buf[2048];
    char bili_uid[32];
    char updataInterval[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Stock", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Stock", APP_MESSAGE_GET_PARAM,
                            (void *)"stock_id", bili_uid);
    app_controller->send_to(SERVER_APP_NAME, "Stock", APP_MESSAGE_GET_PARAM,
                            (void *)"updataInterval", updataInterval);
    sprintf(buf, STOCK_SETTING, bili_uid, updataInterval);
    webpage = buf;
    Send_HTML(webpage);
}

void picture_setting()
{
    char buf[2048];
    char switchInterval[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Picture", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Picture", APP_MESSAGE_GET_PARAM,
                            (void *)"switchInterval", switchInterval);
    sprintf(buf, PICTURE_SETTING, switchInterval);
    webpage = buf;
    Send_HTML(webpage);
}

void media_setting()
{
    char buf[2048];
    char switchFlag[32];
    char powerFlag[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Media", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Media", APP_MESSAGE_GET_PARAM,
                            (void *)"switchFlag", switchFlag);
    app_controller->send_to(SERVER_APP_NAME, "Media", APP_MESSAGE_GET_PARAM,
                            (void *)"powerFlag", powerFlag);
    sprintf(buf, MEDIA_SETTING, switchFlag, powerFlag);
    webpage = buf;
    Send_HTML(webpage);
}

void screen_setting()
{
    char buf[2048];
    char powerFlag[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Screen share", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Screen share", APP_MESSAGE_GET_PARAM,
                            (void *)"powerFlag", powerFlag);
    sprintf(buf, SCREEN_SETTING, powerFlag);
    webpage = buf;
    Send_HTML(webpage);
}

void heartbeat_setting()
{
    char buf[2048];
    char role[32];
    char client_id[32];
    char subtopic[32];
    char mqtt_server[32];
    char port[32];
    char server_user[32];
    char server_password[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_READ_CFG,
                            NULL, NULL);

    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"role", role);
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"client_id", client_id);
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"subtopic", subtopic);
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"mqtt_server", mqtt_server);
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"port", port);
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"server_user", server_user);
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_GET_PARAM,
                            (void *)"server_password", server_password);

    sprintf(buf, HEARTBEAT_SETTING, role, client_id, subtopic, mqtt_server,
            port, server_user, server_password);
    webpage = buf;
    Send_HTML(webpage);
}

void anniversary_setting()
{
    char buf[2048];
    char event_name0[32];
    char target_date0[32];
    char event_name1[32];
    char target_date1[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "Anniversary", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "Anniversary", APP_MESSAGE_GET_PARAM,
                            (void *)"event_name0", event_name0);
    app_controller->send_to(SERVER_APP_NAME, "Anniversary", APP_MESSAGE_GET_PARAM,
                            (void *)"target_date0", target_date0);
    app_controller->send_to(SERVER_APP_NAME, "Anniversary", APP_MESSAGE_GET_PARAM,
                            (void *)"event_name1", event_name1);
    app_controller->send_to(SERVER_APP_NAME, "Anniversary", APP_MESSAGE_GET_PARAM,
                            (void *)"target_date1", target_date1);
    sprintf(buf, ANNIVERSARY_SETTING, event_name0, target_date0, event_name1, target_date1);
    webpage = buf;
    Send_HTML(webpage);
}

void pc_resource_setting()
{
    char buf[2048];
    char pc_ipaddr[32];
    char sensorUpdataInterval[32];
    // 讀取數據
    app_controller->send_to(SERVER_APP_NAME, "PC Resource", APP_MESSAGE_READ_CFG,
                            NULL, NULL);
    app_controller->send_to(SERVER_APP_NAME, "PC Resource", APP_MESSAGE_GET_PARAM,
                            (void *)"pc_ipaddr", pc_ipaddr);
    app_controller->send_to(SERVER_APP_NAME, "PC Resource", APP_MESSAGE_GET_PARAM,
                            (void *)"sensorUpdataInterval", sensorUpdataInterval);
    sprintf(buf, REMOTR_SENSOR_SETTING, pc_ipaddr, sensorUpdataInterval);
    webpage = buf;
    Send_HTML(webpage);
}

void saveSysConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));

    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"ssid_0",
                            (void *)server.arg("ssid_0").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"password_0",
                            (void *)server.arg("password_0").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"power_mode",
                            (void *)server.arg("power_mode").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"backLight",
                            (void *)server.arg("backLight").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"rotation",
                            (void *)server.arg("rotation").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"mpu_order",
                            (void *)server.arg("mpu_order").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"auto_calibration_mpu",
                            (void *)server.arg("auto_calibration_mpu").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"auto_start_app",
                            (void *)server.arg("auto_start_app").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveRgbConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));

    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"min_brightness",
                            (void *)server.arg("min_brightness").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"max_brightness",
                            (void *)server.arg("max_brightness").c_str());
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"time",
                            (void *)server.arg("time").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "AppCtrl", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveWeatherConf(void)  //傳送至APP端
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));

    app_controller->send_to(SERVER_APP_NAME, "Weather",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"tianqi_url",
                            (void *)server.arg("tianqi_url").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"tianqi_appid",
                            (void *)server.arg("tianqi_appid").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"tianqi_appsecret",
                            (void *)server.arg("tianqi_appsecret").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"tianqi_addr",
                            (void *)server.arg("tianqi_addr").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"weatherUpdataInterval",
                            (void *)server.arg("weatherUpdataInterval").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"timeUpdataInterval",
                            (void *)server.arg("timeUpdataInterval").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Weather", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveWeatherOldConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));

    app_controller->send_to(SERVER_APP_NAME, "Weather Old",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"cityname",
                            (void *)server.arg("cityname").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather Old",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"language",
                            (void *)server.arg("language").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather Old",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"weather_key",
                            (void *)server.arg("weather_key").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather Old",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"weatherUpdataInterval",
                            (void *)server.arg("weatherUpdataInterval").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Weather Old",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"timeUpdataInterval",
                            (void *)server.arg("timeUpdataInterval").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Weather Old", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveBiliConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Bili",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"bili_uid",
                            (void *)server.arg("bili_uid").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Bili",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"updataInterval",
                            (void *)server.arg("updataInterval").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Bili", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveStockConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Stock",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"stock_id",
                            (void *)server.arg("stock_id").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Stock",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"updataInterval",
                            (void *)server.arg("updataInterval").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Stock", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void savePictureConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Picture",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"switchInterval",
                            (void *)server.arg("switchInterval").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Picture", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveMediaConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Media",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"switchFlag",
                            (void *)server.arg("switchFlag").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Media",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"powerFlag",
                            (void *)server.arg("powerFlag").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Media", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveScreenConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Screen share",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"powerFlag",
                            (void *)server.arg("powerFlag").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Screen share", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveHeartbeatConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"role",
                            (void *)server.arg("role").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"client_id",
                            (void *)server.arg("mqtt_client_id").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"subtopic",
                            (void *)server.arg("mqtt_subtopic").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"mqtt_server",
                            (void *)server.arg("mqtt_server").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"port",
                            (void *)server.arg("mqtt_port").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"server_user",
                            (void *)server.arg("mqtt_user").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"server_password",
                            (void *)server.arg("mqtt_password").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Heartbeat", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void saveAnniversaryConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "Anniversary",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"event_name0",
                            (void *)server.arg("event_name0").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Anniversary",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"target_date0",
                            (void *)server.arg("target_date0").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Anniversary",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"event_name1",
                            (void *)server.arg("event_name1").c_str());
    app_controller->send_to(SERVER_APP_NAME, "Anniversary",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"target_date1",
                            (void *)server.arg("target_date1").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "Anniversary", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void savePCResourceConf(void)
{
    Send_HTML(F("<h1>設置成功! 退出APP或者繼續其他設置.</h1>"));
    app_controller->send_to(SERVER_APP_NAME, "PC Resource",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"pc_ipaddr",
                            (void *)server.arg("pc_ipaddr").c_str());
    app_controller->send_to(SERVER_APP_NAME, "PC Resource",
                            APP_MESSAGE_SET_PARAM,
                            (void *)"sensorUpdataInterval",
                            (void *)server.arg("sensorUpdataInterval").c_str());
    // 持久化資料
    app_controller->send_to(SERVER_APP_NAME, "PC Resource", APP_MESSAGE_WRITE_CFG,
                            NULL, NULL);
}

void File_Delete()
{
    Send_HTML(
        F("<h3>Enter filename to delete</h3>"
          "<form action='/delete_result' method='post'>"
          "<input type='text' name='delete_filepath' placeHolder='絕對路徑 /image/...'><br>"
          "</label><input class=\"btn\" type=\"submit\" name=\"Submie\" value=\"確認刪除\"></form>"
          "<a href='/'>[Back]</a>"));
}

void delete_result(void)
{
    String del_file = server.arg("delete_filepath");
    boolean ret = tf.deleteFile(del_file);
    if (ret)
    {
        webpage = "<h3>Delete succ!</h3><a href='/delete'>[Back]</a>";
        tf.listDir("/image", 250);
    }
    else
    {
        webpage = "<h3>Delete fail! Please check up file path.</h3><a href='/delete'>[Back]</a>";
    }
    tf.listDir("/image", 250);
    Send_HTML(webpage);
}

void File_Download()
{ // This gets called twice, the first pass selects the input, the second pass then processes the command line arguments
    if (server.args() > 0)
    { // Arguments were received
        if (server.hasArg("download"))
            sd_file_download(server.arg(0));
    }
    else
        SelectInput("Enter filename to download", "download", "download");
}

void sd_file_download(const String &filename)
{
    if (sd_present)
    {
        File download = tf.open("/" + filename);
        if (download)
        {
            server.sendHeader("Content-Type", "text/text");
            server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
            server.sendHeader("Connection", "close");
            server.streamFile(download, "application/octet-stream");
            download.close();
        }
        else
            ReportFileNotPresent(String("download"));
    }
    else
        ReportSDNotPresent();
}

void File_Upload()
{
    tf.listDir("/image", 250);

    webpage = webpage_header;
    webpage += "<h3>Select File to Upload</h3>"
               "<FORM action='/fupload' method='post' enctype='multipart/form-data'>"
               "<input class='buttons' style='width:40%' type='file' name='fupload' id = 'fupload' value=''><br>"
               "<h3>提示:僅可上傳240*240尺寸的JPG(圖片)或MJPEG(影片)檔，請參照說明書使用隨附的轉檔軟體</h3>"
               "<br><button class='buttons' style='width:10%' type='submit'>Upload File</button><br>"
               "<a href='/'>[Back]</a><br><br>";
    webpage += webpage_footer;
    server.send(200, "text/html", webpage);
}

File UploadFile;
void handleFileUpload()
{                                                   // upload a new file to the Filing system
    HTTPUpload &uploadFileStream = server.upload(); // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
                                                    // For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
    String filename = uploadFileStream.filename;
    if (uploadFileStream.status == UPLOAD_FILE_START)
    {
        // 判斷附檔名
        if (filename.endsWith(".jpg") || filename.endsWith(".JPG"))
        {
            filename = "/image/" + filename;
        }
        else if (filename.endsWith(".mjpeg") || filename.endsWith(".MJPEG"))
        {
            filename = "/movie/" + filename;
        }
        else
        {
            Serial.println(F("Unsupported file type"));
            server.send(400, "text/plain", "Unsupported file type");
            return;
        }

        Serial.print(F("Upload File Name: "));
        Serial.println(filename);
        tf.deleteFile(filename);                    // Remove a previous version, otherwise data is appended the file again
        UploadFile = tf.open(filename, FILE_WRITE); // Open the file for writing in SPIFFS (create it, if doesn't exist)
    }
    else if (uploadFileStream.status == UPLOAD_FILE_WRITE)
    {
        if (UploadFile)
            UploadFile.write(uploadFileStream.buf, uploadFileStream.currentSize); // Write the received bytes to the file
    }
    else if (uploadFileStream.status == UPLOAD_FILE_END)
    {
        if (UploadFile) // If the file was successfully created
        {
            UploadFile.close(); // Close the file again
            Serial.print(F("Upload Size: "));
            Serial.println(uploadFileStream.totalSize);
            webpage = webpage_header;
            webpage += F("<h3>File was successfully uploaded</h3>");
            webpage += F("<h2>Uploaded File Name: ");
            webpage += filename + "</h2>";
            webpage += F("<h2>File Size: ");
            webpage += file_size(uploadFileStream.totalSize) + "</h2><br>";
            webpage += webpage_footer;
            server.send(200, "text/html", webpage);
            tf.listDir("/image", 250);
        }
        else
        {
            ReportCouldNotCreateFile(String("upload"));
        }
    }
}

void SelectInput(String heading, String command, String arg_calling_name)
{
    webpage = F("<h3>");
    webpage += heading + "</h3>";
    webpage += F("<FORM action='/");
    webpage += command + "' method='post'>"; // Must match the calling argument e.g. '/chart' calls '/chart' after selection but with arguments!
    webpage += F("<input type='text' name='");
    webpage += arg_calling_name;
    webpage += F("' value=''><br>");
    webpage += F("<type='submit' name='");
    webpage += arg_calling_name;
    webpage += F("' value=''><br>");
    webpage += F("<a href='/'>[Back]</a>");
    Send_HTML(webpage);
}

void ReportSDNotPresent()
{
    webpage = F("<h3>No SD Card present</h3>");
    webpage += F("<a href='/'>[Back]</a><br><br>");
    Send_HTML(webpage);
}

void ReportFileNotPresent(const String &target)
{
    webpage = F("<h3>File does not exist</h3>");
    webpage += F("<a href='/");
    webpage += target + "'>[Back]</a><br><br>";
    Send_HTML(webpage);
}

void ReportCouldNotCreateFile(const String &target)
{
    webpage = F("<h3>Could Not Create Uploaded File (write-protected?)</h3>");
    webpage += F("<a href='/");
    webpage += target + "'>[Back]</a><br><br>";
    Send_HTML(webpage);
}

