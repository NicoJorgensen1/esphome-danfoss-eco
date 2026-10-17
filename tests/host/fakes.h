#pragma once
// Only the ESPHome/hardware boundary is simulated. Tests compile the real
// component, command queue, data packing and encryption implementation.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "esphome/components/climate/climate_mode.h"
using esp_bd_addr_t = uint8_t[6];
using esp_gatt_if_t = int;
using esp_gatt_status_t = int;
constexpr int ESP_OK=0, ESP_GATT_OK=0, ESP_GATT_AUTH_REQ_NONE=0, ESP_GATT_WRITE_TYPE_RSP=0;
enum esp_gattc_cb_event_t { ESP_GATTC_CONNECT_EVT, ESP_GATTC_OPEN_EVT, ESP_GATTC_CLOSE_EVT, ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_SEARCH_CMPL_EVT, ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_READ_CHAR_EVT };
struct esp_ble_gattc_cb_param_t {
 struct gattc_read_char_evt_param {int status=0; uint16_t handle=0,conn_id=0,value_len=0; uint8_t *value=nullptr;} read;
 struct gattc_write_evt_param {int status=0; uint16_t handle=0,conn_id=0;} write;
 struct Event {int status=0,conn_id=0,reason=0; uint8_t remote_bda[6]{};} connect,open,close,disconnect,search_cmpl;
};
namespace fake {
struct Request {bool write; uint16_t handle; std::vector<uint8_t> bytes;};
inline std::vector<Request> requests;
inline bool reject_next=false;
inline int record(bool write,uint16_t handle,const uint8_t *data=nullptr,size_t size=0) {
 if(reject_next){reject_next=false;return 1;}
 requests.push_back({write,handle,{}});
 if(size) requests.back().bytes.assign(data,data+size);
 return 0;
}
}
inline int esp_ble_gattc_read_char(int,int,uint16_t h,int){return fake::record(false,h);}
inline int esp_ble_gattc_write_char(int,int,uint16_t h,uint16_t n,uint8_t *p,int,int){return fake::record(true,h,p,n);}
inline int esp_ble_gap_stop_scanning(){return 0;}
namespace esphome {
using std::optional;
inline uint32_t fnv1_hash(const std::string &){return 1;}
inline std::string format_hex_pretty(const uint8_t *,size_t){return {};}
namespace setup_priority {constexpr float DATA=0;}
class Component {
 public:
 virtual ~Component()=default;
 virtual void setup(){} virtual void loop(){} virtual void dump_config(){}
 virtual float get_setup_priority() const{return 0;}
 bool error=false,warning=false,failed=false;
 std::map<std::string,std::function<void()>> timers;
 void set_timeout(const char *n,uint32_t,std::function<void()> f){timers[n]=std::move(f);}
 bool cancel_timeout(const char *n){return timers.erase(n)>0;}
 void fire(const std::string &n){auto f=timers.at(n);timers.erase(n);f();}
 bool status_has_error()const{return error;} void status_set_error(){error=true;} void status_clear_error(){error=false;}
 void status_set_warning(){warning=true;} void status_clear_warning(){warning=false;} void mark_failed(){failed=true;}
};
class PollingComponent:public Component{public:virtual void update(){}};
namespace sensor {struct Sensor{float state=NAN;void publish_state(float v){state=v;}};}
namespace binary_sensor {struct BinarySensor{bool state=false;void publish_state(bool v){state=v;}};}
namespace climate {
struct ClimateTraits {void set_feature_flags(int){} void set_supported_modes(std::initializer_list<ClimateMode>){} void set_visual_temperature_step(float){}};
struct ClimateCall {
 optional<float> temperature; optional<ClimateMode> mode;
 const optional<float>& get_target_temperature()const{return temperature;}
 const optional<ClimateMode>& get_mode()const{return mode;}
};
class Climate {
 public:
 virtual ~Climate()=default; virtual ClimateTraits traits(){return {};}
 ClimateMode mode=CLIMATE_MODE_AUTO; float target_temperature=NAN,current_temperature=NAN;
 ClimateAction action=ClimateAction::CLIMATE_ACTION_IDLE; int publications=0;
 const std::string& get_name()const{static std::string s="Test";return s;}
 float visual_min=10,visual_max=30;
 void publish_state(){++publications;} void set_visual_min_temperature_override(float v){visual_min=v;} void set_visual_max_temperature_override(float v){visual_max=v;}
 protected: virtual void control(const ClimateCall&){}
};
}
struct ESPPreferenceObject {template<class T>bool load(T*){return false;}template<class T>bool save(T*){return true;}};
struct Preferences {template<class T>ESPPreferenceObject make_preference(uint32_t,bool){return {};}void sync(){}};
inline Preferences fake_preferences; inline Preferences* global_preferences=&fake_preferences;
namespace ble_device_base{constexpr size_t UUID_STR_LEN=37;}
namespace esp32_ble_tracker {
enum class ClientState {INIT,IDLE,DISCOVERED,CONNECTING,CONNECTED,ESTABLISHED,DISCONNECTING};
struct ESPBTUUID {
 uint16_t handle=0;
 static ESPBTUUID from_raw(const char *s){return {static_cast<uint16_t>(std::stoul(std::string(s).substr(6,2),nullptr,16)+20)};}
 static ESPBTUUID from_uint32(uint32_t n){return {static_cast<uint16_t>(n)};}
 const char *to_str(char *p)const{p[0]=0;return p;}
};
}
namespace ble_client {
using esp32_ble_tracker::ClientState;
class BLEClient;
struct BLEClientNode {
 BLEClient *client=nullptr; ClientState node_state=ClientState::INIT;
 BLEClient* parent(){return client;}
 virtual void gattc_event_handler(esp_gattc_cb_event_t,esp_gatt_if_t,esp_ble_gattc_cb_param_t*){}
};
class BLEClient {
 public:
 bool enabled=true; ClientState current=ClientState::IDLE; BLEClientNode *node=nullptr;
 uint8_t address[6]{}; struct Characteristic{uint16_t handle;}; Characteristic chr{};
 uint64_t get_address(){return 0;} uint8_t *get_remote_bda(){return address;}
 const char *address_str(){return "test";}
 int get_gattc_if(){return 1;} int get_conn_id(){return 1;}
 Characteristic *get_characteristic(esp32_ble_tracker::ESPBTUUID,esp32_ble_tracker::ESPBTUUID c){chr.handle=c.handle;return &chr;}
 ClientState state()const{return current;}
 void set_state(ClientState s){current=s;if(node)node->node_state=s;}
 void set_enabled(bool b){enabled=b;if(!b && current!=ClientState::IDLE)set_state(ClientState::DISCONNECTING);}
 void connect(){set_state(ClientState::CONNECTING);}
};
}
}
