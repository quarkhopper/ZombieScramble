//////////////////////////////////////////////////////////////////////////
// Name:       ZombieScramble.ino
// Created:	10/2/2018 3:51:36 PM
// Author:     quarkhopper
//
// Version: 0.5.0
// 0.2.1: increased LED brightness 50 -> 80
// 0.2.2: Number of pixels 4 -> 2. Minor code cleanup
// 0.3.0: Remote game mode 0 holds all games from any rssi and resets when not detected.
//		Attempted using SPIFFS for game mode memory but it does not appear to read or write correctly.
//		Minor code cleanup.
// 0.3.1: Changed zombie destiny chance to 1/3
// 0.4.0 (BRAINCASE): Players start dead and REQUIRE the game reset node (0) to start.
//		This to discourage players from resetting their controllers to cheat.
//		Added admin ability to change mode of near players by setting own mode
//		to 10 + desired mode affect.
//		Holding mode (6) added instead of being dead.
//		All admin effects are for near range only (mode 0 was for max range).
// 0.4.1: Added admin device mode- enables admin to manage game start as <prefix>0.
//		Added more utils for dealing with color tuple <--> uint32 conversions.
// 0.5.0 (VIRUSMEAT): First version with eeprom support for saving game state.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <map>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <ArduinoJson.h>

#include "Utils.h"

#define PIXEL_PIN     1
#define SCL_PIN 2
#define SDA_PIN 0
#define NUM_PIXELS    4
#define MAX_BRIGHTNESS 80
#define EEPROM_I2C_ADDRESS 0x50
#define JSON_BUFFER_SIZE 200

// If true device will start as mode 0, latch when it detects >0 others
// holding, and then unlatches when 0 others holding detected, switching
// to mode 1;
const bool adminDevice = true;

const char* gameSSIDPrefix = "VIRUSMEAT_"; // changed with every 0.x version
const char* gamePass = "eatyourbrain";

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
float currentBrightness = 1; // SIGNED because it's tested for negativity before being used to set
uint8_t indicatorColor[3]; // Color of the indicator LEDs
uint32_t flashingDelay = 0; // set to non-zero when flashing is needed
int32_t lastFlashChange = -10000; // for toggling the flashing
float pulseDelta = 0; // amount currentBrightness changes per tick
float pulseMultiplier = 1; // for directionality
float adminColorAngle = 0; // for admin rainbow effect
float adminColorSpeed = PI/100;

bool playersHoldingLatch = false; // true when >0 others near are holding.

int32_t gameStart = millis(); // when all ticks can officially start
int32_t lastAnimationTick = -10000; // do it now
uint32_t animationTickDelay = 10; // delay between frames
// When the cooldown period for the powerup officially starts.
// Players start out in game mode 2 - cooldown
int32_t powerUpCooldownStart = millis();
int32_t powerUpStart = -10000; // when the powerup (zombie killer) mode started
uint32_t zombieDestinyDelay = 3000; // delay before turning into a zombie after game start
uint8_t zombieDestinyChance = 3; // 1/3 chance of starting out as a zombie
uint32_t powerUpCooldownDuration = 30000; // 30 second cooldown after powerup
uint32_t powerUpDuration = 30000; // 30 seconds of powerup

bool zombieDestiny = false; // destined to become a zombie after game start
// GAME MODES:
// 0 (rainbow): admin mode
// 1 (solid red): human in cooldown (can't powerup)
// 2 (fast pulsing red): human ready for powerup
// 3 (flashing yellow): powerup active (zombie killer)
// 4 (pulsing green): zombie
// 5 (slow pulsing blue): DEAD
// 6 (very fast pulsing violate): Holding for game start
uint8_t gameMode = 5; // the mode the player is in

int32_t nearRSSI = (-50); // at this distance we respond

// Handle changes to game mode that have to to with timers and reaction to
// other players being near. Nearness is determined by RSSI (signal strength)
// and the other player's game mode, read from the end of the SSID.
void DoGameTick(int networkCount) {
	// Actions having to do with timed modes
	if (gameMode == 3 && millis() - powerUpStart > powerUpDuration) {
		SetGameMode(1);
		powerUpCooldownStart = millis();
	}
	else if (gameMode == 1 && millis() - powerUpCooldownStart > powerUpCooldownDuration) SetGameMode(2);

	//Serial.printf("SCAN:\n", networkCount);

	bool holdGameRenewed = false;
	bool playersStillHolding = false;
	for (int i = 0; i < networkCount; i++) {
		String ssid = WiFi.SSID(i);
		// make sure the ssid begins with the game prefix
		if (strncmp(ssid.c_str(), gameSSIDPrefix, strlen(gameSSIDPrefix)) != 0) continue;
		// get the game mode off the end
		int otherGameMode = atoi(ssid.substring(strlen(gameSSIDPrefix)).c_str());

		int32_t rssi = WiFi.RSSI(i);
		if (rssi == 0) continue;

		//String mac = WiFi.BSSIDstr(i);
		//Serial.printf("network number %i: SSID:%s, MAC:%s RSSID:%i\n", i + 1, ssid.c_str(), mac.c_str(), rssi);

		//Serial.printf("Detected player [RSSI: %i, Mode: %i]\n", rssi, otherGameMode);


		if (rssi >= nearRSSI) {
			// We're near another player. Do game mode
			// interaction.

			if (otherGameMode == 6) {
				playersHoldingLatch = true;
				playersStillHolding = true;
			}

			if (otherGameMode > 10) {
				resetGame(otherGameMode - 10, true);
			}

			if (otherGameMode == 0) {
				holdGame();
				holdGameRenewed = true;
			}

			if ((gameMode == 1 || gameMode == 2) && otherGameMode == 4) {
				// Zombification
				SetGameMode(4);
			}
			else if (gameMode == 2 && otherGameMode == 2) {
				// Zombie killer
				SetGameMode(3);
				powerUpStart = millis();
			}
			else if (gameMode == 4 && otherGameMode == 3) {
				// Death for zombie :(
				SetGameMode(5);
			}
		}
	}
	// NOTE: THIS IS THE ONLY WAY THE GAME OFFICIALLY STARTS for this player!
	if ((gameMode == 6 && !holdGameRenewed) ||
	(gameMode == 0 && playersHoldingLatch && !playersStillHolding)) {
		resetGame(1, true);
	}

	DoAnimationTick();
}

// Do animation of the LEDs
void DoAnimationTick() {
	lastAnimationTick = millis();
	strip.clear();
	// Modify brightness if pulsing
	if (pulseDelta != 0) {
		currentBrightness +=  pulseDelta * pulseMultiplier;
		if (currentBrightness < 0) { // if totally black
			currentBrightness = 0;
			pulseMultiplier = 1; // reverse direction (brighten)
		}
		else if (currentBrightness > 1) { // if full brightness
			currentBrightness = 1;
			pulseMultiplier = -1; // reverse direction (fade)
		}
	}
	
	// modify brightness if flashing
	if (flashingDelay > 0 && millis() - lastFlashChange > flashingDelay) {
		if (currentBrightness == 0) currentBrightness = 1;
		else currentBrightness = 0;
		lastFlashChange = millis();
	}

	// update the pixels and show them on the strip.
	// if we're in gameMode 0, update the color.
	if (gameMode == 0) {
		fillStrip(Utils::GetAngleColor(adminColorAngle));
		adminColorAngle += adminColorSpeed;
	}
	else fillStrip(SetColorBrightness(indicatorColor, currentBrightness));
	strip.show();
}

// Set the pixel parameters based on the game mode.
// This is done every time the game mode changes.
void UpdatePixelParameters() {
	currentBrightness = 1;
	lastFlashChange = -10000;
	switch (gameMode) {
		case 0:
		flashingDelay = 0;
		pulseDelta = 0;
		break;

		case 1:
		// red for human
		indicatorColor[0] = 255; // red
		indicatorColor[1] = 0; // green
		indicatorColor[2] = 0; // blue
		flashingDelay = 0;
		pulseDelta = 0;
		break;

		case 2:
		// red fast pulsing ready to power up
		indicatorColor[0] = 255;
		indicatorColor[1] = 0;
		indicatorColor[2] = 0;
		flashingDelay = 0;
		pulseDelta = .08;
		break;

		case 3:
		// yellow fast flashing zombie killer
		indicatorColor[0] = 255;
		indicatorColor[1] = 200;
		indicatorColor[2] = 0;
		flashingDelay = 50;
		pulseDelta = 0;
		break;

		case 4:
		// green zombie
		indicatorColor[0] = 0;
		indicatorColor[1] = 255;
		indicatorColor[2] = 0;
		flashingDelay = 0;
		pulseDelta = .02;
		break;

		case 5:
		// blue dead
		indicatorColor[0] = 0;
		indicatorColor[1] = 0;
		indicatorColor[2] = 255;
		flashingDelay = 0;
		pulseDelta = .01;
		break;

		case 6:
		// holding - waiting for game start
		indicatorColor[0] = 255;
		indicatorColor[1] = 0;
		indicatorColor[2] = 255;
		flashingDelay = 0;
		pulseDelta = .05;
		break;
		
		default:
		// error code
		indicatorColor[0] = 255;
		indicatorColor[1] = 0;
		indicatorColor[2] = 255;
		flashingDelay = 300;
		pulseDelta = 0;
		break;
	}
}

uint32_t SetColorBrightness(uint8_t* indicatorColor, float brightness) {
	return Utils::int32GetRGB(indicatorColor[0] * brightness,
	indicatorColor[1] * brightness,
	indicatorColor[2] * brightness);
}

// Sets the game mode for this player and appends the new game mode
// to the end of the SSID
void SetGameMode(uint8_t newGameMode) {
	if (newGameMode == gameMode) return;
	gameMode = newGameMode;

	char newSSID[200];
	sprintf(newSSID, "%s%i", gameSSIDPrefix, newGameMode);
	//Serial.printf("Set game mode: %i, new SSID: %s\n", newGameMode, newSSID);
	WiFi.softAP(newSSID, gamePass);

	UpdatePixelParameters();
}

void resetGame(int gameMode, bool resetTimes) {
	SetGameMode(gameMode);
	zombieDestiny = random(0, zombieDestinyChance) == 0;
	if (resetTimes) {
		gameStart = millis();
		powerUpCooldownStart = millis();
		powerUpStart = -10000;
	}
}

void holdGame() {
	if (gameMode == 6) return;
	SetGameMode(6); // holding
	zombieDestiny = false;
}

// Read write methods thanks to hkhijhe
/* https://playground.arduino.cc/code/I2CEEPROM */

void i2c_eeprom_write_byte( int deviceaddress, unsigned int eeaddress, byte data ) {
	int rdata = data;
	Wire.beginTransmission(deviceaddress);
	Wire.write((int)(eeaddress >> 8)); // MSB
	Wire.write((int)(eeaddress & 0xFF)); // LSB
	Wire.write(rdata);
	Wire.endTransmission();
}

// WARNING: address is a page address, 6-bit end will wrap around
// also, data can be maximum of about 30 bytes, because the Wire library has a buffer of 32 bytes
void i2c_eeprom_write_page( int deviceaddress, unsigned int eeaddresspage, byte* data, byte length ) {
	Wire.beginTransmission(deviceaddress);
	Wire.write((int)(eeaddresspage >> 8)); // MSB
	Wire.write((int)(eeaddresspage & 0xFF)); // LSB
	byte c;
	for ( c = 0; c < length; c++)
	Wire.write(data[c]);
	Wire.endTransmission();
}

byte i2c_eeprom_read_byte( int deviceaddress, unsigned int eeaddress ) {
	byte rdata = 0xFF;
	Wire.beginTransmission(deviceaddress);
	Wire.write((int)(eeaddress >> 8)); // MSB
	Wire.write((int)(eeaddress & 0xFF)); // LSB
	Wire.endTransmission();
	Wire.requestFrom(deviceaddress,1);
	if (Wire.available()) rdata = Wire.read();
	return rdata;
}

// maybe let's not read more than 30 or 32 bytes at a time!
void i2c_eeprom_read_buffer( int deviceaddress, unsigned int eeaddress, byte *buffer, int length ) {
	Wire.beginTransmission(deviceaddress);
	Wire.write((int)(eeaddress >> 8)); // MSB
	Wire.write((int)(eeaddress & 0xFF)); // LSB
	Wire.endTransmission();
	Wire.requestFrom(deviceaddress,length);
	int c = 0;
	for ( c = 0; c < length; c++ )
	if (Wire.available()) buffer[c] = Wire.read();
}

// For codes that end normal execution
void flashEndCode(int code) {
	if (code > 10) {
		while(1) {
			strip.clear();
			fillStrip(Utils::int32GetRGB(0, 0, 255));
			strip.show();
			delay(100);
			strip.clear();
			strip.show();
			delay(100);

		}
	}

	while(1) {
		for (int i = 0; i < code; i++) {
			strip.clear();
			fillStrip(Utils::int32GetRGB(255, 0, 255));
			strip.show();
			delay(200);
			strip.clear();
			strip.show();
			delay(200);
		}
		delay(2000);
	}
}

void fillStrip(uint32_t color) {
	for (int i = 0; i < NUM_PIXELS; i++) {
		strip.setPixelColor(i, color);
	}
}

void setup() {
	randomSeed(RANDOM_REG32);
	
	UpdatePixelParameters();
	strip.setBrightness(MAX_BRIGHTNESS);
	strip.begin();

	Wire.begin(SDA_PIN, SCL_PIN);

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	DynamicJsonBuffer jsonBuffer(JSON_BUFFER_SIZE);

	char stateData[] = "{\"mode\":5}";
	i2c_eeprom_write_page(EEPROM_I2C_ADDRESS, 0, (byte *)stateData, sizeof(stateData));
	delay(1000);
	char jsonString[JSON_BUFFER_SIZE];
	i2c_eeprom_read_buffer(EEPROM_I2C_ADDRESS, 0, (byte *)jsonString, sizeof(jsonString));
	JsonObject& dataRoot = jsonBuffer.parseObject(jsonString);
	if (!dataRoot.success()) flashEndCode(1);
	else flashEndCode((int)dataRoot["mode"]);
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////

	WiFi.mode(WIFI_AP_STA);
	if (adminDevice) {
		resetGame(0, false);
		zombieDestiny = false;
	}
	else resetGame(5, false);

	WiFi.begin();
	WiFi.scanNetworksAsync(DoGameTick);
}

void loop() {
	auto result = WiFi.scanComplete();
	if(result != WIFI_SCAN_RUNNING && result != WIFI_SCAN_FAILED) WiFi.scanNetworksAsync(DoGameTick);

	if (millis() - lastAnimationTick > animationTickDelay) {
		DoAnimationTick();
	}

	if (zombieDestiny && gameMode != 4 && gameMode != 5 && millis() - gameStart > zombieDestinyDelay) {
		SetGameMode(4); // randomly zombified at game start
	}
}