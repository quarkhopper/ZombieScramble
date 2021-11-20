/*
 * Utils.cpp
 *
 * Created: 9/25/2018 6:03:01 PM
 *  Author: quark
 */ 
 #include "Utils.h"

bool Utils::DoRandomly(uint8_t freq) {
	if (freq == 255) return true;
	else if (freq == 0) return false;
	return random(255) <= freq;
}

uint32_t Utils::GetRandomAngleColor(int resolution) {
	return Utils::GetAngleColor(((2*PI)/resolution) * random(resolution));
}

uint32_t Utils::GetAngleColor(float angle) {
	return Utils::int32GetRGB(
	cos(angle) * 255,
	cos(angle - ((2*PI)/3)) * 255,
	cos(angle - ((4*PI)/3)) * 255);
}

uint32_t Utils::int32GetRGB(uint8_t red, uint8_t green, uint8_t blue) {
	return (red << 16) + (green << 8) + blue;
}

uint8_t* Utils::tupleGetRGB(uint32_t color) {
		uint8_t colorTuple[3];
		colorTuple[0] = color >> 16;
		colorTuple[1] = color >> 8;
		colorTuple[2] = color;
		return colorTuple;	    
}
