#include "fakes.h"
#include "device.h"
#include <cassert>
#include <iostream>
using namespace esphome;
using namespace danfoss_eco;
using namespace climate;
using esphome::esp32_ble_tracker::ClientState;

class TestDevice:public Device {
 public:
 using Device::control;
 using Device::write_pin;
 void initialize(ble_client::BLEClient &c,bool keyed=true){
  client=&c;c.node=this;setup();
  uint8_t key[16]{};if(keyed)set_secret_key(key,false);
  for(auto &p:properties)p->init_handle(&c);
  establish();
 }
 void establish(){parent()->set_state(ClientState::ESTABLISHED);}
 uint16_t temperature_handle(){return p_temperature->handle;}
 uint16_t settings_handle(){return p_settings->handle;}
 uint16_t key_handle(){return p_secret_key->handle;}
 uint16_t pin_handle(){return p_pin->handle;}
 void close_link(){
  parent()->set_state(ClientState::DISCONNECTING);
  esp_ble_gattc_cb_param_t p;gattc_event_handler(ESP_GATTC_DISCONNECT_EVT,1,&p);
 }
 void recover(){parent()->set_state(ClientState::IDLE);fire("retry");establish();}
 void ordinary_read(uint16_t handle){
  if(handle==p_battery->handle)packet(handle,{73});
  else if(handle==p_temperature->handle)packet(handle,{38,38,0,0,0,0,0,0});
  else if(handle==p_settings->handle)packet(handle,{0x44,20,60,10,1,16,0,0,0,0,0,0,0,0,0,0});
  else if(handle==p_errors->handle)packet(handle,{0,0,0,0,0,0,0,0});
  else assert(false);
 }
 void seed(){
  uint8_t temp[8]={38,38};encrypt(xxtea,temp,8);p_temperature->update_state(temp,8);
  uint8_t settings[16]={0x44,20,60,10,1,16};encrypt(xxtea,settings,16);p_settings->update_state(settings,16);
 }
 void packet(uint16_t handle,const std::vector<uint8_t> &raw,int status=0){
  auto bytes=raw;if(!bytes.empty())encrypt(xxtea,bytes.data(),bytes.size());
  esp_ble_gattc_cb_param_t p;p.read.handle=handle;p.read.status=status;p.read.value=bytes.data();p.read.value_len=bytes.size();
  gattc_event_handler(ESP_GATTC_READ_CHAR_EVT,1,&p);
 }
 void ack(uint16_t handle,int status=0){esp_ble_gattc_cb_param_t p;p.write.handle=handle;p.write.status=status;gattc_event_handler(ESP_GATTC_WRITE_CHAR_EVT,1,&p);}
 std::vector<uint8_t> plaintext(fake::Request r){decrypt(xxtea,r.bytes.data(),r.bytes.size());return r.bytes;}
 void settings_packet(const std::vector<uint8_t>& v){auto b=v;p_settings->update_state(b.data(),b.size());}
};

int main(int argc,char **argv){
 assert(argc==2);std::string test=argv[1];
 // ESPHome components live for the process lifetime.
 text_sensor::TextSensor delivery;
 auto *d=new TestDevice;d->set_command_status(&delivery);ble_client::BLEClient c;d->initialize(c);d->seed();fake::requests.clear();
 if(test=="confirmed-mode"){
  d->control({{},CLIMATE_MODE_HEAT});
  assert(d->mode==CLIMATE_MODE_AUTO);
 }else if(test=="short-settings"){
  std::vector<uint8_t> short_packet(8);d->settings_packet(short_packet);
  assert(d->mode==CLIMATE_MODE_AUTO);
 }else if(test=="one-request"){
  d->update();d->loop();assert(fake::requests.size()==1);
 }else if(test=="pending-target"){
  d->control({21.0f,{}});
  // A later state read must not replace the user's pending desired target.
  d->packet(d->temperature_handle(),{38,38,0,0,0,0,0,0});d->loop();
  if(!fake::requests.back().write){d->packet(d->temperature_handle(),{38,38,0,0,0,0,0,0});d->loop();}
  assert(fake::requests.back().write);
  assert(d->plaintext(fake::requests.back())[0]==42);
 }else if(test=="transient-pin"){
  d->write_pin();d->ack(d->pin_handle(),133);assert(!d->failed);assert(d->timers.count("retry"));
 }else if(test=="request-deadline"){
  d->update();d->loop();assert(d->timers.count("request"));
  d->fire("request");assert(d->warning);assert(d->timers.count("retry"));
 }else if(test=="rejected-request"){
  d->control({21.0f,{}});fake::reject_next=true;d->loop();
  assert(d->warning);assert(d->timers.count("retry"));
 }else if(test=="verify-mode"){
  d->control({{},CLIMATE_MODE_HEAT});d->loop();
  assert(!fake::requests.back().write);
  d->packet(d->settings_handle(),{0x44,20,60,10,1,16,0,0,0,0,0,0,0,0,0,0});d->loop();
  assert(fake::requests.back().write);assert(d->mode==CLIMATE_MODE_AUTO);
  d->ack(d->settings_handle());assert(d->mode==CLIMATE_MODE_AUTO);d->loop();
  assert(!fake::requests.back().write);
  d->packet(d->settings_handle(),{0x44,20,60,10,0,16,0,0,0,0,0,0,0,0,0,0});
  assert(d->mode==CLIMATE_MODE_HEAT);
 }else if(test=="latest-target"){
  d->control({21.0f,{}});d->control({22.0f,{}});d->loop();
  assert(fake::requests.size()==1 && !fake::requests.back().write);
  d->packet(d->temperature_handle(),{38,38,0,0,0,0,0,0});d->loop();
  assert(d->plaintext(fake::requests.back())[0]==44);
 }else if(test=="poll-coalescing"){
  for(int i=0;i<1000;++i)d->update();
  for(int i=0;i<4;++i){d->loop();assert(fake::requests.size()==size_t(i+1));d->ordinary_read(fake::requests.back().handle);}
  d->loop();assert(fake::requests.size()==4);assert(!c.enabled);
 }else if(test=="disconnect-recovery"){
  d->control({21.0f,{}});d->loop();d->close_link();
  assert(d->timers.count("retry"));assert(!d->timers.count("request"));
  d->recover();d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  assert(d->plaintext(fake::requests.back())[0]==42);
 }else if(test=="newer-request-survives-retry"){
  d->control({21.0f,{}});d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  d->control({22.0f,{}});d->ack(d->temperature_handle(),133);d->recover();d->loop();
  d->ordinary_read(d->temperature_handle());d->loop();
  assert(d->plaintext(fake::requests.back())[0]==44);
 }else if(test=="bounded-retries"){
  d->update();
  int tries=0;
  do {fake::reject_next=true;d->loop();++tries;if(!d->timers.count("retry"))break;d->recover();}while(tries<20);
  assert(tries>1 && tries<20);assert(d->warning);assert(!c.enabled);assert(!d->failed);
  d->establish();d->loop();assert(fake::requests.empty());
 }else if(test=="unexpected-response"){
  d->control({21.0f,{}});d->loop();
  d->ack(d->temperature_handle());d->packet(d->settings_handle(),{0});
  d->loop();assert(fake::requests.size()==1);assert(d->timers.count("request"));
  d->ordinary_read(d->temperature_handle());d->loop();assert(fake::requests.back().write);
 }else if(test=="mismatched-readback"){
  d->control({21.0f,{}});d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  d->ack(d->temperature_handle());d->loop();d->ordinary_read(d->temperature_handle());
  assert(d->target_temperature==19);assert(d->warning);assert(d->timers.count("retry"));
 }else if(test=="vacation-is-not-schedule"){
  d->control({{},CLIMATE_MODE_AUTO});d->loop();d->ordinary_read(d->settings_handle());d->loop();
  d->ack(d->settings_handle());d->loop();
  d->packet(d->settings_handle(),{0x44,20,60,10,3,16,0,0,0,0,0,0,0,0,0,0});
  assert(d->warning);assert(d->timers.count("retry"));
 }else if(test=="poll-does-not-reset-retries"){
  d->update();int tries=0;
  do{d->update();fake::reject_next=true;d->loop();++tries;if(!d->timers.count("retry"))break;d->recover();}while(tries<20);
  assert(tries==5);assert(d->warning);
 }else if(test=="discovery-deadline"){
  c.set_state(ClientState::IDLE);d->control({21.0f,{}});
  c.set_state(ClientState::CONNECTED);esp_ble_gattc_cb_param_t p;
  d->gattc_event_handler(ESP_GATTC_CONNECT_EVT,1,&p);
  assert(d->timers.count("session"));d->fire("session");
  assert(d->timers.count("retry"));assert(!c.enabled);
 }else if(test=="late-pin-response"){
  c.set_state(ClientState::CONNECTED);d->write_pin();d->fire("pin");
  d->ack(d->pin_handle());assert(d->node_state!=ClientState::ESTABLISHED);
 }else if(test=="failed-poll-does-not-lose-control"){
  d->update();d->loop();auto battery=fake::requests.back().handle;
  d->control({21.0f,{}});d->packet(battery,{},133);
  if(d->timers.count("retry"))d->recover();
  d->loop();assert(fake::requests.back().handle==d->temperature_handle());
  d->ordinary_read(d->temperature_handle());d->loop();assert(fake::requests.back().write);
  assert(d->plaintext(fake::requests.back())[0]==42);
 }else if(test=="control-during-normal-close"){
  d->update();for(int i=0;i<4;++i){d->loop();d->ordinary_read(fake::requests.back().handle);}d->loop();
  d->control({21.0f,{}});d->close_link();assert(!d->timers.count("retry"));
  c.set_state(ClientState::IDLE);d->fire("reconnect");d->establish();d->loop();
  assert(fake::requests.back().handle==d->temperature_handle());
 }else if(test=="pairing-precedes-control"){
  auto *unpaired=new TestDevice;ble_client::BLEClient other;unpaired->initialize(other,false);
  unpaired->control({21.0f,{}});other.set_state(ClientState::CONNECTED);unpaired->write_pin();unpaired->ack(unpaired->pin_handle());
  unpaired->control({22.0f,CLIMATE_MODE_HEAT});unpaired->loop();
  assert(fake::requests.back().handle==unpaired->key_handle());
  unpaired->packet(unpaired->key_handle(),std::vector<uint8_t>(16));unpaired->loop();
  assert(fake::requests.back().handle==unpaired->temperature_handle());
  unpaired->ordinary_read(unpaired->temperature_handle());unpaired->loop();
  assert(fake::requests.back().write);assert(unpaired->plaintext(fake::requests.back())[0]==44);
 }else if(test=="inconsistent-limits-preserve-mode"){
  // Real basement reply: length16, status0, scheduled mode1, min100/max68.
  // Bad limits must not hide a valid mode or be changed by a mode command.
  d->mode=CLIMATE_MODE_HEAT;d->control({{},CLIMATE_MODE_AUTO});d->loop();
  std::vector<uint8_t> observed={0x44,100,68,10,1,16,0,0,0,0,0,0,0,0,0,0};
  d->packet(d->settings_handle(),observed);
  assert(d->mode==CLIMATE_MODE_AUTO);assert(d->visual_min<d->visual_max);
  d->loop();assert(fake::requests.back().write);
  assert(d->plaintext(fake::requests.back())==observed);
  d->ack(d->settings_handle());d->loop();d->packet(d->settings_handle(),observed);
  assert(!d->warning);assert(!d->timers.count("retry"));
  d->control({{},CLIMATE_MODE_HEAT});d->loop();d->packet(d->settings_handle(),observed);d->loop();
  auto expected=observed;expected[4]=0;
  assert(d->plaintext(fake::requests.back())==expected);
 }else if(test=="control-survives-five-failures"){
  d->control({21.0f,{}});
  for(int i=0;i<6;++i){
   fake::reject_next=true;d->loop();
   assert(d->timers.count("retry"));d->recover();
  }
  d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  assert(fake::requests.back().write);
  assert(d->plaintext(fake::requests.back())[0]==42);
 }else if(test=="expired-control-never-writes"){
  d->control({21.0f,{}});fake::now=900000;d->loop();
  assert(d->warning);assert(fake::requests.empty());
  d->establish();d->loop();assert(fake::requests.empty());
 }else if(test=="poll-cannot-extend-control-deadline"){
  d->control({21.0f,{}});d->loop();
  fake::now=899000;d->update();fake::now=900000;d->loop();
  d->ordinary_read(d->temperature_handle());d->loop();
  for(const auto &r:fake::requests)assert(!r.write);
  assert(d->warning);
 }else if(test=="delivery-status-readback"){
  assert(delivery.state=="unknown");
  d->control({21.0f,{}});assert(delivery.state=="pending");
  d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  d->ack(d->temperature_handle());assert(delivery.state=="pending");d->loop();
  d->packet(d->temperature_handle(),{42,38,0,0,0,0,0,0});
  assert(delivery.state=="confirmed");assert(d->target_temperature==21);
 }else if(test=="custom-timeout-sticky-failure"){
  d->set_command_timeout(120000);d->control({21.0f,{}});
  fake::now=119999;d->loop();assert(delivery.state=="pending");
  fake::now=120000;d->loop();assert(delivery.state=="failed");
  c.set_state(ClientState::IDLE);d->update();d->establish();
  for(int i=0;i<4;++i){d->loop();d->ordinary_read(fake::requests.back().handle);}
  assert(delivery.state=="failed");
  d->control({22.0f,{}});assert(delivery.state=="pending");
 }else if(test=="expiry-prepare" || test=="expiry-write" || test=="expiry-verify" || test=="expiry-backoff" || test=="expiry-connecting"){
  d->set_command_timeout(60000);
  if(test=="expiry-connecting")c.set_state(ClientState::IDLE);
  d->control({21.0f,{}});
  if(test!="expiry-connecting")d->loop();
  if(test=="expiry-write" || test=="expiry-verify"){
   d->ordinary_read(d->temperature_handle());d->loop();
   if(test=="expiry-verify"){d->ack(d->temperature_handle());d->loop();}
  }
  if(test=="expiry-backoff")d->fire("request");
  auto count=fake::requests.size();fake::now=60000;
  // A late reply must check the deadline even before the next main loop.
  if(test=="expiry-write")d->ack(d->temperature_handle());
  else if(test=="expiry-prepare" || test=="expiry-verify")d->packet(d->temperature_handle(),{42,38,0,0,0,0,0,0});
  else d->loop();
  assert(delivery.state=="failed");
  d->ordinary_read(d->temperature_handle());d->ack(d->temperature_handle());
  d->establish();d->loop();assert(fake::requests.size()==count);
  assert(!d->timers.count("request") && !d->timers.count("retry"));
 }else if(test=="newer-request-has-own-deadline"){
  d->set_command_timeout(60000);d->control({21.0f,{}});d->loop();
  fake::now=59000;d->control({22.0f,{}});
  d->ordinary_read(d->temperature_handle());d->loop();
  assert(!fake::requests.back().write); // old PREPARE must not proceed to WRITE
  fake::now=60000;d->ordinary_read(d->temperature_handle());d->loop();
  assert(d->plaintext(fake::requests.back())[0]==44);assert(delivery.state=="pending");
  d->ack(d->temperature_handle());d->loop();d->packet(d->temperature_handle(),{44,38,0,0,0,0,0,0});
  assert(delivery.state=="confirmed");
 }else if(test=="superseded-readback-cannot-confirm"){
  d->control({21.0f,{}});d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  d->ack(d->temperature_handle());d->loop();d->control({22.0f,{}});
  d->packet(d->temperature_handle(),{42,38,0,0,0,0,0,0});assert(delivery.state=="pending");
  d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  assert(d->plaintext(fake::requests.back())[0]==44);
 }else if(test=="independent-setting-deadlines"){
  d->set_command_timeout(60000);d->control({21.0f,{}});d->loop();
  fake::now=59000;d->control({{},CLIMATE_MODE_HEAT});
  fake::now=60000;d->loop();assert(delivery.state=="pending");
  c.set_state(ClientState::IDLE);d->fire("reconnect");d->establish();d->loop();
  assert(fake::requests.back().handle==d->settings_handle());
  d->ordinary_read(d->settings_handle());d->loop();d->ack(d->settings_handle());d->loop();
  d->packet(d->settings_handle(),{0x44,20,60,10,0,16,0,0,0,0,0,0,0,0,0,0});
  assert(delivery.state=="failed"); // mode confirmed, earlier target expired
 }else if(test=="millis-wraparound"){
  fake::now=UINT32_MAX-30000;d->set_command_timeout(60000);d->control({21.0f,{}});
  fake::now=29998;d->loop();assert(delivery.state=="pending");
  fake::now=29999;d->loop();assert(delivery.state=="failed");
 }else if(test=="capped-control-backoff"){
  d->control({21.0f,{}});
  for(int i=0;i<260;++i){
   fake::reject_next=true;d->loop();assert(d->timers.count("retry"));
   assert(d->delays.at("retry")==uint32_t(i<5 ? 2000u<<i : 60000u));d->recover();
  }
 }else if(test=="invalid-call-is-atomic"){
  d->control({21.0f,CLIMATE_MODE_OFF});d->loop();
  assert(delivery.state=="failed");assert(fake::requests.empty());
 }else if(test=="thermostats-have-independent-deadlines"){
  text_sensor::TextSensor other_status;auto *other=new TestDevice;ble_client::BLEClient other_client;
  other->set_command_status(&other_status);other->initialize(other_client);other->seed();
  d->set_command_timeout(60000);other->set_command_timeout(120000);
  d->control({21.0f,{}});other->control({22.0f,{}});fake::now=60000;
  d->loop();other->loop();assert(delivery.state=="failed" && other_status.state=="pending");
  other->ordinary_read(other->temperature_handle());other->loop();
  assert(other->plaintext(fake::requests.back())[0]==44);
 }else if(test=="callback-replaces-verifying-command"){
  d->control({21.0f,{}});d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  d->ack(d->temperature_handle());d->loop();
  d->on_state=[&](){d->control({22.0f,{}});};
  d->packet(d->temperature_handle(),{42,38,0,0,0,0,0,0});d->on_state=nullptr;
  assert(delivery.state=="pending");
  d->loop();d->ordinary_read(d->temperature_handle());d->loop();
  assert(fake::requests.back().write && d->plaintext(fake::requests.back())[0]==44);
  d->ack(d->temperature_handle());d->loop();d->packet(d->temperature_handle(),{44,38,0,0,0,0,0,0});
  assert(delivery.state=="confirmed");
 }else if(test=="callback-expires-verifying-command"){
  d->set_command_timeout(60000);d->control({21.0f,{}});
  d->loop();d->ordinary_read(d->temperature_handle());d->loop();d->ack(d->temperature_handle());d->loop();
  d->on_state=[&](){fake::now=60000;d->control({22.0f,{}});};
  d->packet(d->temperature_handle(),{42,38,0,0,0,0,0,0});d->on_state=nullptr;
  assert(delivery.state=="pending");c.set_state(ClientState::IDLE);d->fire("reconnect");d->establish();d->loop();
  d->ordinary_read(d->temperature_handle());d->loop();
  assert(fake::requests.back().write && d->plaintext(fake::requests.back())[0]==44);
 }else if(test=="new-request-publishes-after-acceptance"){
  d->set_command_timeout(60000);d->control({21.0f,{}});fake::now=60000;
  bool transient_failure=false;delivery.on_state=[&](){if(delivery.state=="failed")transient_failure=true;};
  d->control({22.0f,{}});
  assert(!transient_failure && delivery.state=="pending");
 }else return 2;
 std::cout<<test<<": passed\n";
}
