/*
 * Utils.h
 *
 * Created: 9/25/2018 12:02:10 AM
 *  Author: quark
 */ 
 #include <Arduino.h>

#ifndef UTILS_H_
#define UTILS_H_

class Utils
{
	public:
	static bool DoRandomly(uint8_t frequency);
	static uint32_t GetRandomAngleColor(int resolution);
	static uint32_t GetAngleColor(float angle);
	static uint32_t int32GetRGB(uint8_t red, uint8_t green, uint8_t blue);
	static uint8_t* tupleGetRGB(uint32_t color);

	private:
	Utils() {} // static, can't construct
};


#endif /* UTILS_H_ */