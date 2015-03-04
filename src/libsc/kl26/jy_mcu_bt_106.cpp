/*
 * jy_mcu_bt_106.cpp
 * JY-MCU BT Board v1.06
 *
 * Author: Ming Tsang
 * Copyright (c) 2014-2015 HKUST SmartCar Team
 * Refer to LICENSE for details
 */

#include <cstdint>

#include "libbase/kl26/pin.h"
#include "libbase/kl26/uart.h"

#include "libsc/config.h"
#include "libsc/kl26/jy_mcu_bt_106.h"

using namespace libbase::kl26;

namespace libsc
{
namespace kl26
{

Uart::Config JyMcuBt106::Initializer::GetUartConfig() const
{
	Uart::Config product = UartDevice::Initializer::GetUartConfig();
	// On this board, there's a diode connected to the Tx pin, preventing the
	// module to correctly send data to the MCU
	product.rx_config[Pin::Config::kPullEnable] = true;
	product.rx_config[Pin::Config::kPullUp] = true;
	return product;
}

JyMcuBt106::JyMcuBt106(const Config &config)
		: UartDevice(Initializer(config))
{}

}
}