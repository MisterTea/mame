// license:BSD-3-Clause
// copyright-holders:AJR
/***************************************************************************

    Strike Zone (1994)
    Hoop Shot (undumped)

    Redemption games by Purple Star Inc. using infrared light curtains.

***************************************************************************/

#include "emu.h"
#include "cpu/z80/z80.h"
#include "cpu/i86/i186.h"

class strkzn_state : public driver_device
{
public:
	strkzn_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_lightcpu(*this, "lightcpu")
	{ }

	void strkzn(machine_config &config);
	void light_io(address_map &map);
	void light_mem(address_map &map);
	void main_io(address_map &map);
	void main_mem(address_map &map);
private:
	required_device<cpu_device> m_maincpu;
	required_device<cpu_device> m_lightcpu;
};

void strkzn_state::main_mem(address_map &map)
{
	map(0x0000, 0xdfff).rom().region("maincpu", 0);
	map(0xe000, 0xffff).ram();
}

void strkzn_state::main_io(address_map &map)
{
	map.global_mask(0xff);
}

void strkzn_state::light_mem(address_map &map)
{
	map(0x00000, 0x00fff).ram();
	map(0xf0000, 0xfffff).rom().region("lightcpu", 0);
}

void strkzn_state::light_io(address_map &map)
{
	map(0x0007, 0x0007).nopr();
}

MACHINE_CONFIG_START(strkzn_state::strkzn)
	MCFG_CPU_ADD("maincpu", Z80, 4000000)
	MCFG_CPU_PROGRAM_MAP(main_mem)
	MCFG_CPU_IO_MAP(main_io)

	MCFG_CPU_ADD("lightcpu", I80188, 10000000)
	MCFG_CPU_PROGRAM_MAP(light_mem)
	MCFG_CPU_IO_MAP(light_io)
MACHINE_CONFIG_END

INPUT_PORTS_START( strkzn )
INPUT_PORTS_END

ROM_START( strkzn )
	ROM_REGION(0x10000, "maincpu", 0)
	ROM_LOAD( "strkzn08",  0x00000, 0x10000, CRC(cc217dd6) SHA1(a5e9261c5c3f6d57f34ffd6019227d616f0c59bc) )

	ROM_REGION(0x10000, "lightcpu", 0)
	ROM_LOAD( "strkzn01",  0x00000, 0x10000, CRC(d408582e) SHA1(96a54ebe67db952a77b732f5ab345a94834d0906) )

	ROM_REGION(0x80000, "soundrom", 0) // OKIM6373???
	ROM_LOAD( "strkznu16", 0x00000, 0x80000, CRC(67f7674b) SHA1(451a26da55315fcaccdc02817521c78acdd8eb8a) )
ROM_END

GAME( 1994, strkzn, 0, strkzn, strkzn, strkzn_state, 0, ROT0, "Purple Star", "Strike Zone (Purple Star)", MACHINE_IS_SKELETON_MECHANICAL )
