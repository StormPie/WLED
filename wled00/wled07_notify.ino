/*
 * UDP notifier
 */

#define WLEDPACKETSIZE 24

void notify(byte callMode, bool followUp=false)
{
  if (!udpConnected) return;
  switch (callMode)
  {
    case 0: return;
    case 1: if (!notifyDirect) return; break;
    case 2: if (!notifyButton) return; break;
    case 4: if (!notifyDirect) return; break;
    case 6: if (!notifyDirect) return; break; //fx change
    case 7: if (!notifyHue) return; break;
    case 8: if (!notifyDirect) return; break;
    default: return;
  }
  byte udpOut[WLEDPACKETSIZE];
  udpOut[0] = 0; //0: wled notifier protocol 1: WARLS protocol
  udpOut[1] = callMode;
  udpOut[2] = bri;
  udpOut[3] = col[0];
  udpOut[4] = col[1];
  udpOut[5] = col[2];
  udpOut[6] = nightlightActive;
  udpOut[7] = nightlightDelayMins;
  udpOut[8] = effectCurrent;
  udpOut[9] = effectSpeed;
  udpOut[10] = white;
  udpOut[11] = 4; //compatibilityVersionByte: 0: old 1: supports white 2: supports secondary color 3: supports FX intensity, 24 byte packet 4: supports transitionDelay
  udpOut[12] = colSec[0];
  udpOut[13] = colSec[1];
  udpOut[14] = colSec[2];
  udpOut[15] = whiteSec;
  udpOut[16] = effectIntensity;
  udpOut[17] = (transitionDelay >> 0) & 0xFF;
  udpOut[18] = (transitionDelay >> 8) & 0xFF;
  
  IPAddress broadcastIp;
  broadcastIp = ~WiFi.subnetMask() | WiFi.gatewayIP();

  notifierUdp.beginPacket(broadcastIp, udpPort);
  notifierUdp.write(udpOut, WLEDPACKETSIZE);
  notifierUdp.endPacket();
  notificationSentCallMode = callMode;
  notificationSentTime = millis();
  notificationTwoRequired = (followUp)? false:notifyTwice;
}

void arlsLock(uint32_t timeoutMs)
{
  if (!arlsTimeout){
     strip.setRange(0, ledCount-1, 0);
     strip.setMode(0);
  }
  arlsTimeout = true;
  arlsTimeoutTime = millis() + timeoutMs;
}

void handleNotifications()
{
  //send second notification if enabled
  if(udpConnected && notificationTwoRequired && millis()-notificationSentTime > 250){
    notify(notificationSentCallMode,true);
  }

  //unlock strip when realtime UDP times out
  if (arlsTimeout && millis() > arlsTimeoutTime)
  {
    strip.unlockAll();
    if (bri == 0) strip.setBrightness(0);
    arlsTimeout = false;
    strip.setMode(effectCurrent);
  }

  //receive UDP notifications
  if(udpConnected && (receiveNotifications || receiveDirect)){
    uint16_t packetSize = notifierUdp.parsePacket();

    //hyperion / raw RGB
    if (!packetSize && receiveDirect && udpRgbConnected) {
      packetSize = rgbUdp.parsePacket();
      if (packetSize > 1026 || packetSize < 3) return;
      byte udpIn[packetSize];
      rgbUdp.read(udpIn, packetSize);
      arlsLock(5200);
      uint16_t id = 0;
      for (uint16_t i = 0; i < packetSize -2; i += 3)
      {
        if (useGammaCorrectionRGB)
        {
          strip.setPixelColor(id, gamma8[udpIn[i]], gamma8[udpIn[i+1]], gamma8[udpIn[i+2]]);
        } else {
          strip.setPixelColor(id, udpIn[i+0], udpIn[i+1], udpIn[i+2]);
        }
        id++; if (id >= ledCount) break;
      }
      strip.show();
    }
    
    if (packetSize > 1026) return;
    if(packetSize && notifierUdp.remoteIP() != WiFi.localIP()) //don't process broadcasts we send ourselves
    {
      byte udpIn[packetSize];
      notifierUdp.read(udpIn, packetSize);
      if (udpIn[0] == 0 && !arlsTimeout && receiveNotifications) //wled notifier, block if realtime packets active
      {
        if (receiveNotificationColor)
        {
        col[0] = udpIn[3];
        col[1] = udpIn[4];
        col[2] = udpIn[5];
        }
        if (udpIn[11] > 0 && receiveNotificationColor) //check if sending modules white val is inteded
        {
          white = udpIn[10];
          if (udpIn[11] > 1 )
          {
            colSec[0] = udpIn[12];
            colSec[1] = udpIn[13];
            colSec[2] = udpIn[14];
            whiteSec = udpIn[15];
          }
        }
        if (udpIn[8] != effectCurrent && receiveNotificationEffects)
        {
          effectCurrent = udpIn[8];
          strip.setMode(effectCurrent);
        }
        if (udpIn[9] != effectSpeed && receiveNotificationEffects)
        {
          effectSpeed = udpIn[9];
          strip.setSpeed(effectSpeed);
        }
        if (udpIn[11] > 2 && udpIn[16] != effectIntensity && receiveNotificationEffects)
        {
          effectIntensity = udpIn[16];
          strip.setIntensity(effectIntensity);
        }
        if (udpIn[11] > 3)
        {
          transitionDelayTemp = ((udpIn[17] << 0) & 0xFF) + ((udpIn[18] << 8) & 0xFF00);
        }
        nightlightActive = udpIn[6];
        if (!nightlightActive)
        {
          if (receiveNotificationBrightness) bri = udpIn[2];
          colorUpdated(3);
        }
      }  else if (udpIn[0] > 0 && udpIn[0] < 4 && receiveDirect) //1 warls //2 drgb //3 drgbw
      {
        if (packetSize > 1) {
          if (udpIn[1] == 0)
          {
            arlsTimeout = false;
          } else {
            arlsLock(udpIn[1]*1000);
          }
          if (udpIn[0] == 1) //warls
          {
            for (uint16_t i = 2; i < packetSize -3; i += 4)
            {
              if (udpIn[i] + arlsOffset < ledCount && udpIn[i] + arlsOffset >= 0)
              if (useGammaCorrectionRGB)
              {
                strip.setPixelColor(udpIn[i] + arlsOffset, gamma8[udpIn[i+1]], gamma8[udpIn[i+2]], gamma8[udpIn[i+3]]);
              } else {
                strip.setPixelColor(udpIn[i] + arlsOffset, udpIn[i+1], udpIn[i+2], udpIn[i+3]);
              }
            }
          } else if (udpIn[0] == 2) //drgb
          {
            uint16_t id = 0;
            for (uint16_t i = 2; i < packetSize -2; i += 3)
            {
              if (useGammaCorrectionRGB)
              {
                strip.setPixelColor(id, gamma8[udpIn[i]], gamma8[udpIn[i+1]], gamma8[udpIn[i+2]]);
              } else {
                strip.setPixelColor(id, udpIn[i+0], udpIn[i+1], udpIn[i+2]);
              }
              id++; if (id >= ledCount) break;
            }
          } else if (udpIn[0] == 3) //drgbw
          {
            uint16_t id = 0;
            for (uint16_t i = 2; i < packetSize -3; i += 4)
            {
              if (useGammaCorrectionRGB)
              {
                strip.setPixelColor(id, gamma8[udpIn[i]], gamma8[udpIn[i+1]], gamma8[udpIn[i+2]], gamma8[udpIn[i+3]]);
              } else {
                strip.setPixelColor(id, udpIn[i+0], udpIn[i+1], udpIn[i+2], udpIn[i+3]);
              }
              id++; if (id >= ledCount) break;
            }
          }
          strip.show();
        }
      }
    }
  }
}


