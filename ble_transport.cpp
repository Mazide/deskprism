#include "ble_transport.h"
#include <esp_random.h>

namespace ble {
bool Transport::begin(QueueHandle_t queue) {
  commands=queue;
  if(!commands || !NimBLEDevice::init("Prism Desk")) return false;
  NimBLEDevice::setSecurityAuth(true,true,true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setMTU(185);
  server=NimBLEDevice::createServer();
  if(!server) return false;
  server->setCallbacks(&serverCallbacks,false);
  server->advertiseOnDisconnect(false);
  NimBLEService* service=server->createService(serviceUuid);
  if(!service) return false;
  auto* rx=service->createCharacteristic(rxUuid,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC|NIMBLE_PROPERTY::WRITE_AUTHEN,protocol::maxMessage);
  tx=service->createCharacteristic(txUuid,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC|NIMBLE_PROPERTY::READ_AUTHEN,512);
  if(!rx || !tx) return false;
  rx->setCallbacks(&rxCallbacks);
  tx->setValue("{\"v\":1,\"ok\":true,\"status\":\"ready\"}");
  service->start();
  auto* advertising=NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(serviceUuid);
  NimBLEAdvertisementData scan;scan.setName("Prism Desk");
  advertising->setScanResponseData(scan);advertising->enableScanResponse(true);
  return true;
}
bool Transport::open(uint32_t now) {
  if(!server) return false;
  openedAt=now;advertisingWindow=true;
  pairingCode=100000+esp_random()%900000;
  NimBLEDevice::setSecurityPasskey(pairingCode.load());
  if(link.load()) return true;
  return NimBLEDevice::getAdvertising()->start();
}
void Transport::update(uint32_t now) {
  if(!server) return;
  auto* advertising=NimBLEDevice::getAdvertising();
  if(advertisingWindow && !windowOpen(now)) {
    advertisingWindow=false;advertising->stop();
    if(link.load() && !authenticated.load()) server->disconnect(activeHandle.load());
  } else if(windowOpen(now) && !link.load() && !advertising->isAdvertising()) advertising->start();
}
void Transport::ServerCallbacks::onConnect(NimBLEServer* server,NimBLEConnInfo& info) {
  if(owner.activeHandle.load()!=BLE_HS_CONN_HANDLE_NONE) {server->disconnect(info.getConnHandle());return;}
  owner.activeHandle=info.getConnHandle();owner.link=true;
  owner.authenticated=false;owner.frame.reset();
  NimBLEDevice::startSecurity(info.getConnHandle());
}
void Transport::ServerCallbacks::onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int) {
  if(info.getConnHandle()!=owner.activeHandle.load()) return;
  owner.activeHandle=BLE_HS_CONN_HANDLE_NONE;
  owner.link=false;owner.authenticated=false;owner.frame.reset();
}
void Transport::ServerCallbacks::onAuthenticationComplete(NimBLEConnInfo& info) {
  if(info.getConnHandle()!=owner.activeHandle.load()) return;
  if(!info.isEncrypted() || !info.isAuthenticated()) {
    owner.server->disconnect(info.getConnHandle());return;
  }
  owner.authenticated=true;
}
void Transport::RxCallbacks::onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& info) {
  if(!info.isEncrypted() || !info.isAuthenticated()) return;
  const auto value=characteristic->getValue();
  protocol::Packet packet;packet.ble=true;
  for(size_t i=0;i<value.size();++i) {
    const auto result=owner.frame.push(char(value.data()[i]),packet.json);
    if(result==protocol::Feed::Message && xQueueSend(owner.commands,&packet,0)!=pdTRUE) owner.rxError=true;
    if(result==protocol::Feed::TooLong) owner.rxError=true;
  }
}
}
