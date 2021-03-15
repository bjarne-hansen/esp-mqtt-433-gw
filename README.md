# MQTT / 433 MHz Gateway

This is a simple gateway built to enable communication with 433 MHz based
devices such as remote controls, door bells, relays, etc.

The gateway is built to run on a ESP8266 based device which has built in
support for TCP/IP communicatiob via WiFi.  Specifically, this project was
built on a WeMos D1 Mini.

The MQTT/433MHz Gateway can be configured to subscribe to a topic on a MQTT
server and receive codes that are sent using a 433 MHz transmitter unit.  The
messages are expected to be JSON formatted like this:

    {"value": 7054607 , "bits": 24}

A message like the one above will send the value 7055607 using a 24 bits.

Likewise, the MQTT/433 MHz Gateway can be configured to publish codes received
from 433 MHz devices to a MQTT topic.  The JSON message are formatted like this:

    {"timestamp":"2021-03-14T16:17:30","value": 7054607,"bits": 24}

The project uses the quite versatile *rc-switch* module for 433 MHz
communication and can easily be amended to other types of 433 MHz communication.

The connection to the MQTT server is done via SSL to keep data confidential,
in particular any client id, username, and password exchanged.

The configuration is stored in a JSON file in a LittleFS filesystem configured
on the ESP8266 device.

The default configuration (shown below), only needs you to specify the name and
password for you WiFi access point before it is uploaded using the LittleFS
filesystem upload tool.

The default configuration uses the server hosted at `test.mosquitto.org` on port
`8883` which is the default SSL port for a MQTT server.  The server hosted at
mosquitto.org does not require a username and password, so they are left as
`null`.

You may want to change the configuration for the `publish` and `subscribe`
topics to avoid any potential conflict with others testing the code.

    {
      "ap": {
        "ssid": "<<access point>>",
        "password": "<<password>>"
      },
      "mqtt": {
        "client_id": "IoT:",
        "host": "test.mosquitto.org",
        "port": 8883,
        "user": null,
        "password": null,
        "publish":   "433gw/data",
        "subscribe": "433gw/cmd"
      },
      "server": {
        "trustedca": "/mosquitto.pem",
        "fingerprint": "EE:BC:4B:F8:57:E3:D3:E4:07:54:23:1E:F0:C8:A1:56:E0:D3:1A:1C"
      }
    }

The fingerprint is the fingerprint of the X509 certificate for the server
`test.mosquitto.org`.  This fingerprint can be obtained using the following
OpenSSL command.

    $ openssl s_client -connect test.mosquitto.org:8883 < /dev/null 2>/dev/null | openssl x509 -fingerprint -noout -in /dev/stdin

As you can see above, the configuration also refers to a `trustedca` file
stored in the LittleFS filesystem.  This file holds the trusted root certificate
of the server at `test.mosquitto.org`.  The root certificate can be extracted
using a browser like Firefox pointing it to `https://test.mosquitto.org:8883`
and then inspecting the certificate returned.
