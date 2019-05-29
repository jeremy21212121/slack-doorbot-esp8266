# Slack DoorBot on ESP8266 w/ Arduino IDE

## Overview
This bot listens for the magic words by slack direct message, then unlocks the door. Actually, it engages a relay, which can turn on/off whatever you connect it to. Lighting? Airhorn? High-voltage arc generator? The sky and your good judgement are the limit.

The intended use case is, however, connecting it to the button that buzzes people into your building.

I started with [urish/arduino-slack-bot](https://github.com/urish/arduino-slack-bot), got it working again, and made significant changes to suit my use case.

## Slack integration
This sketch uses the Slack real-time messaging API. First, it authenticates with the `rtm.connect` method via HTTP API. If successful, the HTTP API returns a websocket URL to connect to. Once established, the websocket connection is maintained with "ping" messages every 5 seconds.

This slackbot only responds to direct messages. Don't invite this bot into public Channels; it will receive every event from that channel and it may become overwhelmed. It is programmed to ignore messages from public channels, but it still needs to parse the JSON. For every message sent, there are usually 3 events that the slackbot must parse: a "user is typing" event, a "message" event, and a "desktop notification" event.

## Software dependencies
- Arduino IDE
- [ESP8266 core for arduino](https://github.com/esp8266/Arduino)
- A modified version of the [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) library[0](#Security) included in this repo
- [ArduinoJson](https://arduinojson.org/) library (V6)

## Hardware
- any dev board based on the ESP8266 SoC should be fine. I used a clone of the [Wemos D1 mini](https://wiki.wemos.cc/products:d1:d1_mini)
- 5v relay module

The relay modules I have are "low-level trigger", which means they are activated by LOW rather than HIGH (0V rather than 5V). I used an NPN transistor connected in such a manner that when pin D2 is HIGH, the relay input is brought LOW. This means the relay is activated when pin D2 is brought HIGH.

Pin D2 is pulled down using a 5k resistor.

A diode and capacitor are used in an attempt to decouple the SoC from the relay module, since the are both powered by the same source. The relatively-large-ish inductive load spike from the relay being enabled can cause instability for the SoC. Electrically, I'm making it up as I go, so I don't know if what I did actually helps. But it has been stable so I'll take it.

## Before flashing
Before flashing this sketch, make sure to `#define` the following constants in a file called `Secrets.h`:

* `SLACK_BOT_TOKEN` - The API token of your slack bot
* `WIFI_SSID` - Your WiFi signal name (SSID)
* `WIFI_PASSWORD` - Your WiFi password


## Security [0]

This sketch makes use of SSL for both HTTPS:// and WSS:// requests. Due to hardware limitations, the certificate fingerprint for HTTPS requests must be provided in the sketch. The current certificate at time of writing expires on Feb 12, 2021. If you are in the future, or slack changes their cert before then, you will need to update the SSL fingerprint.

Previously, SSL for websockets with the `arduinoWebSockets` library on the ESP8266 didn't have this limitation. Due to a change in the SSL library used by ESP8266 core, this is no longer the case. I don't think it is the same cert as for HTTPS, but I really don't know because I didn't try. So, in order to enable WSS:// connections, I have altered the websockets library to disable fingerprint verfication. Data in transit is still encrypted, but it is theoretically possible for a priveleged adversary to perform a man-in-the-middle attack. This is, in my opinion, a perfectly acceptable risk for a trivial slackbot. Doing SSL at all, on an 80mHz processor, is a small miracle.


