/*
 * jy_mcu_bt_106.h
 * JY-MCU BT Board v1.06
 *
 * Author: Ming Tsang
 * Copyright (c) 2014 HKUST SmartCar Team
 * Refer to LICENSE for details
 */

#pragma once

#include <cstdint>

#include "libbase/k60/uart.h"

#include "libsc/k60/uart_device.h"

namespace libsc
{
namespace k60
{

class JyMcuBt106 : public UartDevice
{
public:
	JyMcuBt106(const uint8_t id,
			const libbase::k60::Uart::Config::BaudRate baud_rate);

protected:
	class UartConfigBuilder : public UartDevice::UartConfigBuilder
	{
	public:
		using UartDevice::UartConfigBuilder::UartConfigBuilder;

		virtual libbase::k60::Uart::Config Build() const override;
	};
};

}
}
