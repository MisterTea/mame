// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
/***************************************************************************

    IBM PC AT compatibles 8042 keyboard controller

***************************************************************************/

#ifndef MAME_MACHINE_AT_KEYBC_H
#define MAME_MACHINE_AT_KEYBC_H

#pragma once

#include "cpu/mcs48/mcs48.h"


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> at_keyboard_controller_device

class at_keyboard_controller_device : public device_t
{
public:
	// construction/destruction
	at_keyboard_controller_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto system_reset_cb() { return m_system_reset_cb.bind(); }
	auto gate_a20_cb() { return m_gate_a20_cb.bind(); }
	auto input_buffer_full_cb() { return m_input_buffer_full_cb.bind(); }
	auto output_buffer_empty_cb() { return m_output_buffer_empty_cb.bind(); }
	auto keyboard_clock_cb() { return m_keyboard_clock_cb.bind(); }
	auto keyboard_data_cb() { return m_keyboard_data_cb.bind(); }

	// interface to the host pc
	DECLARE_READ8_MEMBER( data_r );
	DECLARE_WRITE8_MEMBER( data_w );
	DECLARE_READ8_MEMBER( status_r );
	DECLARE_WRITE8_MEMBER( command_w );

	// interface to the keyboard
	DECLARE_WRITE_LINE_MEMBER( keyboard_clock_w );
	DECLARE_WRITE_LINE_MEMBER( keyboard_data_w );

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	virtual const tiny_rom_entry *device_rom_region() const override;
	virtual ioport_constructor device_input_ports() const override;
	virtual void device_add_mconfig(machine_config &config) override;

private:
	// internal 8042 interface
	DECLARE_READ_LINE_MEMBER( t0_r );
	DECLARE_READ_LINE_MEMBER( t1_r );
	DECLARE_READ8_MEMBER( p1_r );
	DECLARE_READ8_MEMBER( p2_r );
	DECLARE_WRITE8_MEMBER( p2_w );

	// internal state
	upi41_cpu_device *m_cpu;

	// interface to the host pc
	devcb_write_line    m_system_reset_cb;
	devcb_write_line    m_gate_a20_cb;
	devcb_write_line    m_input_buffer_full_cb;
	devcb_write_line    m_output_buffer_empty_cb;

	// interface to the keyboard
	devcb_write_line    m_keyboard_clock_cb;
	devcb_write_line    m_keyboard_data_cb;

	uint8_t m_clock_signal;
	uint8_t m_data_signal;
};


// device type definition
DECLARE_DEVICE_TYPE(AT_KEYBOARD_CONTROLLER, at_keyboard_controller_device)

#endif // MAME_MACHINE_AT_KEYBC_H
