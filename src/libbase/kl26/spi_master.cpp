/*
 * spi_master.cpp
 *
 * Author: Harrison Ng
 * Copyright (c) 2014-2015 HKUST SmartCar Team
 * Refer to LICENSE for details
 */

#include "libbase/kl26/hardware.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <functional>

#include "libbase/log.h"
#include "libbase/kl26/clock_utils.h"
#include "libbase/kl26/misc_utils.h"
#include "libbase/kl26/pin.h"
#include "libbase/kl26/pinout.h"
#include "libbase/kl26/sim.h"
#include "libbase/kl26/spi_master.h"
#include "libbase/kl26/spi_utils.h"

#include "libutil/misc.h"

using namespace libutil;

namespace libbase
{
namespace kl26
{

namespace
{

constexpr SPI_Type * MEM_MAPS[PINOUT::GetSpiCount()] = {SPI0, SPI1};

SpiMaster* g_instances[PINOUT::GetSpiCount()] = {};

}

SpiMaster::SpiMaster(const Config &config)
		: m_is_init(false)
{
	if (!InitModule(config) || g_instances[m_module])
	{
		assert(false);
		return;
	}

	m_is_init = true;
	g_instances[m_module] = this;

	Sim::SetEnableClockGate(EnumAdvance(Sim::ClockGate::kSpi0, m_module), true);
	InitPin(config);
	InitBrReg(config);

/*
 * C1 register
 */

//	Enable SPI system
//	Set as master mode
//	Enable interrupt
	MEM_MAPS[m_module]->C1 |= SPI_C1_SPE_MASK | SPI_C1_SPIE_MASK | SPI_C1_MSTR_MASK;

//	CPHA = 1, First edge on SPSCK occurs at the start of the first cycle of a data transfer.
	CLEAR_BIT(MEM_MAPS[m_module]->C1,SPI_C1_CPHA_SHIFT);
//	CPOL = 0, Active-high SPI clock (idles low)
	SET_BIT(MEM_MAPS[m_module]->C1,SPI_C1_CPOL_SHIFT);

/*
 * C2 register
 */
// 	Set SPI mode to 8-bit mode
	CLEAR_BIT(MEM_MAPS[m_module]->C2,SPI_C2_SPIMODE_SHIFT);

//	Enable SPI SS
	SET_BIT(MEM_MAPS[m_module]->C2,SPI_C2_MODFEN_SHIFT);
	SET_BIT(MEM_MAPS[m_module]->C1,SPI_C1_SSOE_SHIFT);

	m_tx_isr = config.tx_isr;
	m_rx_isr = config.rx_isr;
}

SpiMaster::~SpiMaster()
{}

bool SpiMaster::InitModule(const Config &config)
{
	const Spi::MisoName miso = PINOUT::GetSpiMiso(config.sin_pin);
	const int miso_module = (miso != Spi::MisoName::kDisable)
			? static_cast<int>(miso) : -1;

	const Spi::MosiName mosi = PINOUT::GetSpiMosi(config.sout_pin);
	const int mosi_module = (mosi != Spi::MosiName::kDisable)
			? static_cast<int>(mosi) : -1;

	const Spi::SckName sck = PINOUT::GetSpiSck(config.sck_pin);
	const int sck_module = static_cast<int>(sck);

	const Spi::PcsName pcs = PINOUT::GetSpiPcs(config.pcs_pin);
	const int pcs_module = SpiUtils::GetSpiModule(pcs);

	if ((miso == Spi::MisoName::kDisable && mosi == Spi::MosiName::kDisable)
			|| sck == Spi::SckName::kDisable || pcs == Spi::PcsName::kDisable)
	{
		return false;
	}
	if (miso_module != mosi_module && miso_module != -1 && mosi_module != -1)
	{
		return false;
	}
	const int module = (miso_module != -1) ? miso_module : mosi_module;
	if (module == -1)
	{
		return false;
	}
	if (module != sck_module || module != pcs_module)
	{
		return false;
	}

	m_module = module;
	return true;
}

void SpiMaster::InitPin(const Config &config)
{
	if (config.sin_pin != Pin::Name::kDisable)
	{
		Pin::Config sin_config;
		sin_config.pin = config.sin_pin;
		sin_config.mux = PINOUT::GetSpiMisoMux(config.sin_pin);
		m_sin = Pin(sin_config);
	}

	if (config.sout_pin != Pin::Name::kDisable)
	{
		Pin::Config sout_config;
		sout_config.pin = config.sout_pin;
		sout_config.mux = PINOUT::GetSpiMosiMux(config.sout_pin);
		m_sout = Pin(sout_config);
	}

	Pin::Config sck_config;
	sck_config.pin = config.sck_pin;
	sck_config.mux = PINOUT::GetSpiSckMux(config.sck_pin);
	m_sck = Pin(sck_config);

	Pin::Config cs_config;
	cs_config.pin = config.pcs_pin;
	cs_config.mux = PINOUT::GetSpiPcsMux(config.pcs_pin);
	m_cs = Pin(cs_config);
}

void SpiMaster::InitBrReg(const Config &config)
{
	// BaudRateDivisor = (SPPR + 1) * 2 ^ (SPR + 1)
	// BaudRate = SPI Module Clock / BaudRateDivisor

	uint8_t reg = 0;

	uint32_t module_clock = 0;
	if (m_module == 0)
	{
		module_clock = ClockUtils::GetBusClockKhz();
	}
	else if (m_module == 1)
	{
		module_clock = ClockUtils::GetCoreClockKhz();
	}

	Uint best_sppr = 0;
	Uint best_spr = 0;
	Uint min_diff = static_cast<Uint>(-1);
	for (Uint spr = 0; spr < 9; ++spr)
	{
		for (Uint sppr = 0; sppr < 8; ++sppr)
		{
			const Uint divisor = (sppr + 1) * ((2 << spr) + 1);
			const uint32_t this_baud_rate = module_clock / divisor;
			const Uint this_diff = abs((int32_t)(this_baud_rate
					- config.baud_rate_khz));
			if (this_diff < min_diff)
			{
				min_diff = this_diff;
				best_sppr = sppr;
				best_spr = spr;
			}

			if (min_diff == 0)
			{
				break;
			}
		}
	}
	reg |= SPI_BR_SPPR(best_sppr);
	reg |= SPI_BR_SPR(best_spr);

	MEM_MAPS[m_module]->BR = reg;
}

uint16_t SpiMaster:: ExchangeData(const uint8_t, const uint16_t data)
{
	uint16_t received;

//	Wait untill SPTEF = 1
	while(!GET_BIT(MEM_MAPS[m_module]->S,SPI_S_SPTEF_SHIFT));
//	Remark: in 8 bit mode, DH ignored
	MEM_MAPS[m_module]->DH = (data & 0xFF00) >> 8;
	MEM_MAPS[m_module]->DL = data & 0xFF;

//	Wait till transmitt buffer is empty
	while(!GET_BIT(MEM_MAPS[m_module]->S,SPI_S_SPTEF_SHIFT));
//	Remark: in 8 bit mode, DH ignored
	received = (MEM_MAPS[m_module]->DH << 8) | (MEM_MAPS[m_module]->DL);
	return received;
}

void SpiMaster::KickStart()
{}

size_t SpiMaster::PushData(const uint8_t, const uint16_t*, const size_t)
{
	return 0;
}

size_t SpiMaster::PushData(const uint8_t, const uint8_t*, const size_t)
{
	return 0;
}

void SpiMaster::SetEnableRxIrq(const bool)
{
	STATE_GUARD(SpiMaster, VOID);
}

void SpiMaster::SetEnableTxIrq(const bool)
{
	STATE_GUARD(SpiMaster, VOID);
}

}
}
