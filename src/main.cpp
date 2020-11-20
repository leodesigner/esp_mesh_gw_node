#include <Arduino.h>
#include <CommandParser.h>
#include <EspNowFloodingMesh.h>
#include <SimpleMqtt.h>
#include <base64.h>
Commands cmd;

#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

#if defined __has_include
#  if __has_include (<espmesh.h>)
#    include <espmesh.h>
#  else
     // default - change this
     #define AP_PWD          "12345678"
     #define ESP_NOW_CHANNEL 1
     // AES 128bit
     unsigned char secredKey[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                      0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
     unsigned char iv[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                      0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
     int bsid = 0x010101;
     const int ttl = 3;
#  endif
#endif

int channel = ESP_NOW_CHANNEL;

const char deviceName[] = "m";
// this one will not affect actual timeouts
SimpleMQTT simpleMqtt = SimpleMQTT(ttl, deviceName, 12, 40, 10);

#define DEBUG_PRINTS

// node boot time
time_t boot_time;

// -------------------------------------------------------------------------------------------------------------

void setup() {
  Serial.begin(230400);
  cmd.begin(Serial);

  espNowFloodingMesh_enableBlink(LED_BUILTIN, 1);

  // Set to master mode - ack every mqtt message received
  // by default we send ack only for messages with our node name at the beginnig
  // of the topic nodename/sensor/set
  simpleMqtt.set_op_mode(MODE_GW_ACK_ALL);
  // Handle MQTT events from main node.
  simpleMqtt.handleEvents([](const char *src_node, const char *msgid,
                             char command, const char *topic,
                             const char *value) {
    Serial.printf("MQTT %s %s %c %s %s\n", src_node, msgid, command, topic,
                  value);
  });

  // handle raw data
  simpleMqtt.handleEvents_raw(
      [](const uint8_t *data, int len, uint32_t replyId, uint16_t elapsed) {
        char replyStr[15];
        sprintf(replyStr, "%lu", (unsigned long int)replyId);
        cmd.send("REC", replyStr, data, len);
      });

  espNowFloodingMesh_ErrorDebugCB([](int level, const char *str) {
    if (level == 0) {
      cmd.send("ERROR", str);
    }
    if (level == 1) {
      cmd.send("WRN", str);
    }
    if (level == 2) {
      cmd.send("INFO", str);
    }
  });

  Serial.println();
  Serial.println();
  Serial.println();
  cmd.send("READY");
}

char buf[30];
bool initialized = false;

void hexDump2(const uint8_t *b, int len, int step) {
  Serial.println();
  for (int i = 0; i < len; i = i + step) {
    Serial.print("DUMP: ");
    for (int x = 0; x < step && (x + i) < len; x++) {
      if (b[i + x] <= 0xf) Serial.print("0");
      Serial.print(b[i + x], HEX);
      Serial.print(" ");
    }
    Serial.print(" ");
    for (int x = 0; x < step && (x + i) < len; x++) {
      if (b[i + x] <= 32 || b[i + x] >= 126) {
        Serial.print(".");
      } else
        Serial.print((char)b[i + x]);
    }
    Serial.print("\n");
  }
  Serial.print("                   Length: ");
  Serial.println(len);
}

void loop() {
  espNowFloodingMesh_loop();

  const char *lost_msg = simpleMqtt.resend_loop();
  if (lost_msg != NULL) {
    cmd.send("TIMEOUT", lost_msg);
  }

  // Serial command parser
  cmd.handleInputCommands([](const char *cmdName, const char *p1,
                             const char *p2, const char *p3,
                             const unsigned char *binary, int size) {
    if (strcmp(cmdName, "PING") == 0) {
      cmd.send("ACK");

    } else if (strcmp(cmdName, "CHANNEL") == 0) {
      if (strcmp(p1, "SET") == 0) {
        channel = atoi(p2);
        cmd.send("ACK", itoa(channel, buf, 10));

      } else if (strcmp(p1, "GET") == 0) {
        cmd.send("ACK", itoa(channel, buf, 10));
      } else {
        cmd.send("NACK", "PARAM");
      }

    } else if (strcmp(cmdName, "ROLE") == 0) {
      if (strcmp(p1, "MASTER") == 0) {
        int ttl = (p2 == NULL ? 0 : atoi(p2));
        espNowFloodingMesh_setToMasterRole(true, ttl);
        cmd.send("ACKROLE", p2);

      } else if (strcmp(p1, "NODE") == 0) {
        espNowFloodingMesh_setToMasterRole(false);
        cmd.send("ACKROLE");
      } else {
        cmd.send("NACK", "INVALID ROLE");
      }

    } else if (strcmp(cmdName, "SEND") == 0) {
      int ttl = 0;
      if (p1 != 0) {
        ttl = atoi(p1);
      }
      espNowFloodingMesh_send((uint8_t *)binary, size, ttl);
      cmd.send("ACKSEND");

    } else if (strcmp(cmdName, "REQ") == 0) {
      int ttl = 0;
      if (p1 != 0) {
        ttl = atoi(p1);
      }
      uint32_t replyptr = espNowFloodingMesh_sendAndHandleReply(
          (uint8_t *)binary, size, ttl, NULL);
      sprintf(buf, "%lu", (unsigned long int)replyptr);
      cmd.send("ACKREQ", buf);

    } else if (strcmp(cmdName, "REQC") ==
               0)  // REQC ttl timeout try_cnt [message]
    {
      int ttl = 0;
      int timeout = 1000;
      int try_cnt = 3;
      if (p1 != 0 && p2 != 0 && p3 != 0) {
        ttl = atoi(p1);
        timeout = atoi(p2);
        try_cnt = atoi(p3);
      }
      uint32_t replyptr = espNowFloodingMesh_sendAndHandleReply(
          (uint8_t *)binary, size, ttl, NULL);
      // Store message in the cache
      simpleMqtt.mc_add_msg((uint8_t *)binary, size, ttl, replyptr, timeout,
                            try_cnt);
      sprintf(buf, "%lu", (unsigned long int)replyptr);
      cmd.send("ACKREQC", buf);
    } else if (strcmp(cmdName, "REQID") == 0) {
      int ttl = 0;
      if (p1 != 0) {
        ttl = atoi(p1);
      }
      uint32_t umsgid = 1;
      if (p2 != 0) {
        // umsgid = atol(p2);
        sscanf(p2, "%u", &umsgid);
      }
      uint32_t replyptr = espNowFloodingMesh_sendAndHandleReplyUmid(
          (uint8_t *)binary, size, umsgid, ttl, NULL);
      sprintf(buf, "%lu", (unsigned long int)replyptr);
      cmd.send("ACKREQID", buf);

    } else if (strcmp(cmdName, "REPLY") == 0) {
      int ttl = 0;
      uint32_t replyPrt;
      if (p1 == NULL || p2 == NULL) {
        cmd.send("ACKREPLY", "INVALID PARAM");
      } else {
        ttl = atoi(p1);
        replyPrt = Commands::sTolUint(p2);
        espNowFloodingMesh_sendReply((uint8_t *)binary, size, ttl, replyPrt);
        cmd.send("ACKREPLY", buf);
      }

    } else if (strcmp(cmdName, "STOP") == 0) {
      espNowFloodingMesh_end();
      cmd.send("ACKSTOP");
    } else if (strcmp(cmdName, "REBOOT") == 0) {
      cmd.send("ACKREBOOT", "Rebooting");
      Serial.flush();
      ESP.restart();
      while (1)
        ;

    } else if (strcmp(cmdName, "INIT") == 0) {
      if (initialized == false) {
        initialized = true;
        espNowFloodingMesh_secredkey(secredKey);
        espNowFloodingMesh_setAesInitializationVector(iv);
        espNowFloodingMesh_begin(channel, bsid);
        boot_time = espNowFloodingMesh_getRTCTime();
        cmd.send("ACKINIT");
      } else {
        cmd.send("NACK", "REBOOT NEEDED");
      }

    } else if (strcmp(cmdName, "RTC") == 0) {
      if (strcmp(p1, "GET") == 0) {
        time_t t = espNowFloodingMesh_getRTCTime();
        sprintf(buf, "%lu", t);
        cmd.send("ACK", buf);
      } else if (strcmp(p1, "SET") == 0) {
        time_t t = Commands::sTolUint(p2);
        espNowFloodingMesh_setRTCTime(t);
        sprintf(buf, "%lu", t);
        cmd.send("ACKRTC", buf);
      } else {
        cmd.send("NACK", "INVALID PARAMETER");
      }

    } else if (strcmp(cmdName, "KEY") == 0) {
      if (strcmp(p1, "SET") == 0) {
        if (size == sizeof(secredKey)) {
          memcpy(secredKey, binary, sizeof(secredKey));
          espNowFloodingMesh_secredkey(secredKey);
          cmd.send("ACK");
        } else {
          cmd.send("NACK", "SIZE!=16");
        }
      } else if (strcmp(p1, "GET") == 0) {
        cmd.send("ACK", secredKey, sizeof(secredKey));
      } else {
        cmd.send("NACK", "PARAM");
      }

    } else if (strcmp(cmdName, "MEM") == 0) {
      sprintf(buf, "%d", ESP.getFreeHeap());
      cmd.send("ACKMEM", buf);
    } else if (strcmp(cmdName, "IV") == 0) {
      if (strcmp(p1, "SET") == 0) {
        if (size == sizeof(secredKey)) {
          memcpy(iv, binary, sizeof(iv));
          espNowFloodingMesh_setAesInitializationVector(iv);
          cmd.send("ACK");
        } else {
          cmd.send("NACK", "SIZE!=16");
        }
      } else if (strcmp(p1, "GET") == 0) {
        cmd.send("ACK", secredKey, sizeof(secredKey));
      } else {
        cmd.send("NACK", "PARAM");
      }

    } else if (strcmp(cmdName, "BSID") == 0) {
      if (strcmp(p1, "SET") == 0) {
        if (strlen(p2) > 0) {
          bsid = atoi(p2);
          cmd.send("ACK");
        } else {
          cmd.send("NACK", "INVALID SIZE");
        }
      } else if (strcmp(p1, "GET") == 0) {
        sprintf(buf, "%ud", bsid);
        cmd.send("ACKBSID", buf);
      } else {
        cmd.send("NACK", "PARAM");
      }

    } else if (strcmp(cmdName, "MAC") == 0) {
      String mac = WiFi.macAddress();
      cmd.send("ACKMAC", (const unsigned char *)mac.c_str(), 6);

    } else if (strcmp(cmdName, "MQTT") == 0) {
      // MQTT [MQTT NODE/msgid P topic value]
      simpleMqtt.send_async((const char *)binary, size, 0);
      cmd.send("ACKMQTT");

    } else if (strcmp(cmdName, "STATS") == 0) {
      struct telemetry_db_item *tdb = espNowFloodingMesh_get_tdb_ptr();
      bool found = false;
      uint16_t idx = 0;
      while (idx < TELEMETRY_STATS_SIZE && !found) {
        if (tdb[idx].mac_addr[0] == 0 && tdb[idx].mac_addr[1] == 0 &&
            tdb[idx].mac_addr[2] == 0 && tdb[idx].mac_addr[3] == 0 &&
            tdb[idx].mac_addr[4] == 0 && tdb[idx].mac_addr[5] == 0) {
          // found empty spot
          found = true;
          break;
        }
        idx++;
      }
      char b[350];
      int len = idx * sizeof(telemetry_db_item);
      int encoded_len = Base64encode_len(len);
      if (encoded_len >= (int)sizeof(b) - 1) {
        // Serial.println("Base64encode_len data too long.");
      }
      int actual_len = Base64encode(b, (const char *)tdb, len);
      b[actual_len] = '\0';

      cmd.send("STATS", b);
      // Serial.print("Bytes sent: ");
      // Serial.println(idx * sizeof(telemetry_db_item));

    } else if (strcmp(cmdName, "STATS_PKT") == 0) {
#define MAX_JSON_LEN 250
      telemetry_stats_st *telemetry_stats =
          espNowFloodingMesh_get_tmt_stats_ptr();
      char json[MAX_JSON_LEN];
      snprintf(
          json, MAX_JSON_LEN,
          "{\"boot_ts\":%lu,\"sent\":%u,\"recv\":%u,\"dup\":%u,\"fwd\":%u}",
          boot_time, telemetry_stats->sent_pkt, telemetry_stats->received_pkt,
          telemetry_stats->dup_pkt, telemetry_stats->fwd_pkt);
      // telemetry_stats->ttl0_pkt);
      cmd.send("STATS_PKT", json);

    } else if (strcmp(cmdName, "STATS_MQTT") == 0) {
      telemetry_t_st *tsp = simpleMqtt.get_telemetry_t_ptr();
      char json[MAX_JSON_LEN];
      snprintf(json, MAX_JSON_LEN,
               "{\"rtt_min\":%u,{\"rtt_a64\":%u\",{\"rtt_a512\":%u,{\"rtt_"
               "4096\":%u\",\"rtt_max\":%u,\"resend\":%u,\"ack\":%u}",
               tsp->rtt_min, tsp->rtt_avg_x64, tsp->rtt_avg_x512,
               tsp->rtt_avg_x4096, tsp->rtt_max, tsp->resend_pkt, tsp->ack_pkt);
      cmd.send("STATS_MQTT", json);

    } else if (strcmp(cmdName, "MAC_ADDR") == 0) {
      byte mac[6];
      char mac_addr[20];
      WiFi.macAddress(mac);
      sprintf(mac_addr, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
              mac[3], mac[4], mac[5]);
      cmd.send("MAC_ADDR", mac_addr);

    } else if (strncmp(cmdName, "POW_", 4) == 0) {
      const char *p = cmdName + 4;
      float dBm = atof(p);
      WiFi.setOutputPower(dBm);
      cmd.send("ACK");

    } else {
      cmd.send("NACK", "INVALID COMMAND");  // Handle invalid command
    }
  });
}