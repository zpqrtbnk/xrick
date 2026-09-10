/*--------------------------------------------------------------------
	Atari Audio Library v1.10
	Small & accurate ATARI-ST audio emulation
	Arnaud Carré aka Leonard/Oxygene
	@leonard_coder
--------------------------------------------------------------------*/
#pragma once
#include <stdint.h>

class Ym2149c
{
public:

	void	Reset(uint32_t hostReplayRate, uint32_t ymClock = 2000000);
	void	WritePort(uint8_t port, uint8_t value);
	uint8_t ReadPort(uint8_t port) const;
	int16_t	ComputeNextSample();
	void	InsideTimerIrq(bool inside);

	uint16_t GetCurrentVisualLevels() const { return m_currentVisualLevels; } // only used for some player visual, contains 3 YM voices volume and STE DAC in 8888 format
	void MuteVoices(uint32_t muteMask);

private:
	void	WriteReg(int reg, uint8_t value);
	uint16_t Tick();

	static const uint32_t kDcAdjustHistoryBit = 11;	// 2048 values (~20ms at 44Khz) 

	int16_t		dcAdjust(uint16_t v);

	int			m_selectedReg;
	const uint8_t* m_pCurrentEnv;
	uint32_t	m_ymClockOneEighth;
	uint32_t	m_hostReplayRate;
	uint32_t	m_toneCounter[3];
	uint32_t	m_tonePeriod[3];
	uint32_t	m_toneEdges;

	uint32_t	m_envCounter;
	int			m_envPos;
	uint32_t	m_envPeriod;
	uint32_t	m_noiseCounter;
	uint32_t	m_noisePeriod;
	uint32_t	m_toneMask;
	uint32_t	m_noiseMask;
	uint32_t	m_noiseRndRack;
	uint32_t	m_currentNoiseMask;
	uint16_t 	m_currentVisualLevels;
	uint16_t	m_dcAdjustBuffer[1<<kDcAdjustHistoryBit];
	uint32_t	m_dcAdjustPos;
	uint32_t	m_dcAdjustSum;
	uint8_t		m_regs[14];
	uint32_t	m_innerCycle;
	uint32_t 	m_noiseHalf;
	uint32_t 	m_enableMask;
	bool		m_insideTimerIrq;
	bool		m_edgeNeedReset[3];
};
