// Some things need to be included here, seems files are loaded alphabetically
#include <arduino.h>
#include <Update.h>
#include "led_remixer_config.cpp"

// BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define DEBUG  // Comment this line out to remove printf statements in released version
#ifdef DEBUG
#define debugf(...) Serial.print("  <<ble>> ");Serial.printf(__VA_ARGS__);
#define debugf_noprefix(...) Serial.printf(__VA_ARGS__);
#else
#define debugf(...)
#define debugf_noprefix(...)
#endif

// Message Protocol
// Start (1 byte)    = D0
// CommCode (1 byte)
//  Set Brightness   = 02
//  Set Speed        = 03
//  Set Pattern      = 04
// Message-specific payload (1 or more bytes) = <see message examples below>
// End (1 byte)      = D1   

// Set Brightness - 0 (off) to 255 (100%)
//   MessageType = 02
//   Payload = 1 byte
// D0 02 00 D1 (Off)
// D0 02 01 D1 (Very Dim)
// D0 02 80 D1 (Medium)
// D0 03 FF D1 (Very Bright)

// Set Animation Speed (0 to 255 Hz)
//   MessageType = 03
//   Payload = 1 byte
// DO 03 01 D1 (1 frame / sec)
// D0 03 B4 D1 (180 frames / sec; If you swing at 1 rotation per second each frame will be 1 degree)

// Set display pattern
//   MessageType = 04
//   FrameHeight = 1 byte
//   FrameCount = 1 byte
//   Pattern 3 bytes * frameHeight * frameCount = R,G,B (1 byte each)
// D0 04 01 01 FF FF FF D1 (1 Solid Red Pixel)
// D0 04 01 02 FF FF FF 00 00 00 D1 (1 Blinking Red Pixel)
// D0 04 03 03 00 00 FF 00 00 00 00 00 FF 00 00 00 00 00 FF 00 00 00 00 00 FF 00 00 00 00 00 FF D1 (solid blue x)

enum CommCode {
  CC_SUCCESS,           // 0
  CC_ERROR,             // 1
  CC_SET_BRIGHTNESS,    // 2
  CC_SET_SPEED,         // 3
  CC_SET_PATTERN        // 4
};

class LEDRemixerBLE : public BLEServerCallbacks, public BLECharacteristicCallbacks{
  
  private:
    LEDRemixerConfig& config;
    
    // Nordic nRF
    BLEUUID remixerServiceUUID = BLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    BLEUUID remixerRxCharacteristicUUID = BLEUUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    BLEUUID remixerTxCharacteristicUUID = BLEUUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    BLEUUID remixerNotifyCharacteristicUUID = BLEUUID("6E400004-B5A3-F393-E0A9-E50E24DCCA9E");

    BLEServer* server;
    bool deviceConnected = false;
    bool oldDeviceConnected = false;
    
    BLEService* remixerService;
    BLECharacteristic* remixerRxCharacteristic;
    BLECharacteristic* remixerTxCharacteristic;
    BLECharacteristic* remixerNotifyCharacteristic;
    
    void bleSendError(){
      uint8_t response[] = {0x45, 0x46, 0x00, 0x07, CC_ERROR, 0x46, 0x45};
      writeToRemixer(response);
    }
    
    void bleSendSuccess(){
      uint8_t response[] = {0x45, 0x46, 0x00, 0x07, CC_SUCCESS, 0x46, 0x45};
      writeToRemixer(response);
    }

    void bleSendLolcat(){
      debugf("Send error lolcat\n");
      uint8_t response[] = {0x01, 0x02, 0x00, 0x05, 0x01};
      writeToRemixer(response);
    }
    
  public:
    LEDRemixerBLE(LEDRemixerConfig& _config): config(_config) {}

    long bleLastReceived;
    void setup(){
      debugf("Setup begin\n");
      // Create the BLE Device
      BLEDevice::init("LED Remixer ESP32C3");

      // Create the BLE Server
      server = BLEDevice::createServer();
      server->setCallbacks(this);
      
      // Create the Remixer BLE Service
      remixerService = server->createService(remixerServiceUUID);
      remixerTxCharacteristic = remixerService->createCharacteristic(remixerTxCharacteristicUUID, BLECharacteristic::PROPERTY_READ);
      remixerTxCharacteristic->addDescriptor(new BLE2902());
      remixerNotifyCharacteristic = remixerService->createCharacteristic(remixerNotifyCharacteristicUUID, BLECharacteristic::PROPERTY_NOTIFY);
      remixerNotifyCharacteristic->addDescriptor(new BLE2902());
      remixerRxCharacteristic = remixerService->createCharacteristic(remixerRxCharacteristicUUID, BLECharacteristic::PROPERTY_WRITE);
      remixerRxCharacteristic->setCallbacks(this);
      remixerService->start();

      // Start advertising
      server->getAdvertising()->start();
      debugf("Waiting a client connection to notify..\n");
      debugf("Setup complete\n");
    }

    void loop(){      
      // disconnecting
      if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        server->startAdvertising(); // restart advertising
        debugf("start advertising\n");
        oldDeviceConnected = deviceConnected;
      }
      // connecting
      if (deviceConnected && !oldDeviceConnected) {
        debugf("connecting!\n");
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
      }
    }

    void writeToRemixer(uint8_t* data){
      if (deviceConnected) {
        remixerTxCharacteristic->setValue(data, data[2] << 8 | data[3]);
        remixerNotifyCharacteristic->notify();
      }
    }
    
    void onWrite(BLECharacteristic *characteristic) {
      debugf("OnWrite()!\n");
      if(characteristic->getUUID().equals(remixerRxCharacteristicUUID)){
        bleLastReceived = millis();
        uint8_t* bleStatus = characteristic->getData();
        size_t bleLength = characteristic->getLength();
        
        
        debugf("Message incoming!\n");
        debugf("- len = %d\n", bleLength);
        debugf("- msg = ");
        for(int i = 0; i < bleLength; i++){
          debugf_noprefix("0x%x ",bleStatus[i]);
        }
        debugf("\n");

        // Little "Z"
        // int frameHeight = 8
        // int frameCount = 6
//        int zonar[48] = {
//          1, 0, 0, 0, 0, 0, 1, 1,
//          1, 0, 0, 0, 0, 1, 0, 1,
//          1, 0, 0, 0, 1, 0, 0, 1,
//          1, 0, 0, 1, 0, 0, 0, 1,
//          1, 0, 1, 0, 0, 0, 0, 1,
//          1, 1, 0, 0, 0, 0, 0, 1,
//        };
        
        // Big "Z"
        int frameHeight = 20;
        int frameCount = 10;
        int zonar[200] = {
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
        };
//            << the other half to make 20 x 20 "Z" >>
//          1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
//          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//        };
        
        // Process BLE
        if(bleStatus[0] == 0x61 && bleStatus[1] == 0x73 && bleStatus[2] == 0x64 & bleStatus[3] == 0x66){
          debugf("Got ASDF!\n");
          config.setFrameHeight(frameHeight);
          config.setFrameCount(frameCount);
          for(int i=0; i<sizeof(zonar); i++){
            debugf("i=%d\n",i);
            config.pattern[i*3]=0x0;
            config.pattern[i*3+1]=0x0;
            if(zonar[i]==1){
              config.pattern[i*3+2]=0xff;
            } else {
              config.pattern[i*3+2]=0x0;              
            }
          }
          config.patternLength = config.frameHeight*config.frameCount*3;
          config.savePattern();
        }else if(bleStatus[0] == 0xD0 && bleStatus[bleLength - 1] == 0xD1){
          CommCode requestCode = static_cast<CommCode>(bleStatus[1]);
          if(requestCode == CC_SET_BRIGHTNESS){
            config.setLedBrightness(bleStatus[2]);
            bleSendSuccess();
          }else if(requestCode == CC_SET_SPEED){
            config.setAnimationSpeed(bleStatus[2]);
            bleSendSuccess();
          }else if(requestCode == CC_SET_PATTERN){
            for (int i=0; i<sizeof(config.pattern); i++){
              config.pattern[i]=0;
            }
            config.setFrameHeight(bleStatus[2]);
            config.setFrameCount(bleStatus[3]);
            // Need exception handling for buffer overruns!!!
            config.patternLength = config.frameHeight*config.frameCount*3;
            for (int i=0; i<config.patternLength; i++){
              config.pattern[i]=bleStatus[i+4];
            }
            config.savePattern();
            
            bleSendSuccess();
          }
          else{
            bleSendError();
          }
        }else{
          bleSendLolcat();
        }
      }
    }

    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      debugf("onConnect\n");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      debugf("onDisconnect\n");
    }

};
