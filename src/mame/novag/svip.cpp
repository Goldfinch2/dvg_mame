// license:BSD-3-Clause
// copyright-holders:hap
// thanks-to:Berger
/*******************************************************************************

Novag Super VIP (aka Super V.I.P.) (model 895)

NOTE: Turn the power switch off before exiting MAME, otherwise NVRAM won't save
properly.

It's a portable chess computer with keypad input, no embedded chessboard. The
chess engine is similar to other HD6301/3Y ones by David Kittinger. The 'sequels'
of this portable design (Ruby, Sapphire, ..) are on H8.

Hardware notes:
- Hitachi HD63A03YF (mode 2) @ 9.83MHz
- 32KB ROM(TC57256AD-12), 2KB RAM(TC5517CFL-20)
- LCD with 4 digits and custom segments, no LCD chip
- RJ-12 port, 24 buttons, piezo

The LCD is the same as the one in Primo / Supremo / Super Nova.

Super VIP is Novag's first chess computer with a proper serial interface. It
connects to the Novag Super System Distributor, which can then connect to an
external chessboard, TV interface, computer, etc.

Serial transmission format for Novag Super System is 1 start bit, 8 data bits,
1 stop bit, no parity. On Super VIP, the baud rate is selectable 1200 or 9600,
default 9600 for v3.x, 1200 for v1.x.

Known official Novag Super System (or compatible) peripherals:
- Super System Chess Board Auto Sensory
- Super System Chess Board Touch Sensory
- Super System TV-Interface
- Universal Electronic Chess Board (aka UCB)

TODO:
- if/when MAME supports an exit callback, hook up power-off switch to that
- add Super System peripherals, each has their own MCU
- unmapped reads from 0x3* range, same as snova driver
- is (non-super) VIP on the same hardware? minus EPROM or RS232

BTANB:
- just for fun, I'll mention that on many (not all) batches of Super VIP, the
  white text label under the Knight button says "Into" instead of "Info"

*******************************************************************************/

#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/m6800/m6801.h"
#include "machine/nvram.h"
#include "machine/sensorboard.h"
#include "sound/dac.h"
#include "video/pwm.h"

#include "screen.h"
#include "speaker.h"

// internal artwork
#include "novag_svip.lh"


namespace {

class svip_state : public driver_device
{
public:
	svip_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_lcd_pwm(*this, "lcd_pwm"),
		m_dac(*this, "dac"),
		m_rs232(*this, "rs232"),
		m_inputs(*this, "IN.%u", 0),
		m_out_lcd(*this, "s%u.%u", 0U, 0U)
	{ }

	void svip(machine_config &config);

	DECLARE_INPUT_CHANGED_MEMBER(power_off) { if (newval) m_power = false; }
	DECLARE_CUSTOM_INPUT_MEMBER(power_r) { return m_power ? 1 : 0; }

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override { m_power = true; }

private:
	// devices/pointers
	required_device<hd6301y0_cpu_device> m_maincpu;
	required_device<pwm_display_device> m_lcd_pwm;
	required_device<dac_2bit_ones_complement_device> m_dac;
	required_device<rs232_port_device> m_rs232;
	required_ioport_array<4> m_inputs;
	output_finder<4, 10> m_out_lcd;

	bool m_power = false;
	bool m_lcd_strobe = false;
	u8 m_inp_mux = 0;
	u8 m_select = 0;

	void main_map(address_map &map);

	void lcd_pwm_w(offs_t offset, u8 data);
	u8 p2_r();
	void p2_w(u8 data);
	void p5_w(u8 data);
	void p6_w(u8 data);
};

void svip_state::machine_start()
{
	m_out_lcd.resolve();

	// register for savestates
	save_item(NAME(m_power));
	save_item(NAME(m_lcd_strobe));
	save_item(NAME(m_inp_mux));
	save_item(NAME(m_select));
}



/*******************************************************************************
    I/O
*******************************************************************************/

void svip_state::lcd_pwm_w(offs_t offset, u8 data)
{
	m_out_lcd[offset & 0x3f][offset >> 6] = data;
}

u8 svip_state::p2_r()
{
	u8 data = 0;

	// P20,P22: switch state
	data |= m_inputs[3]->read() & 5;

	// P23: serial rx
	data |= m_rs232->rxd_r() ? 0 : 8;

	// P25-P27: multiplexed inputs
	for (int i = 0; i < 3; i++)
		if (m_inp_mux & m_inputs[i]->read())
			data |= 0x20 << i;

	return ~data;
}

void svip_state::p2_w(u8 data)
{
	// P21: 4066 in/out to LCD
	if (m_lcd_strobe && ~data & 2)
	{
		u16 lcd_data = (~m_select << 2 & 0x300) | m_inp_mux;
		m_lcd_pwm->matrix(m_select & 0xf, lcd_data);
	}
	m_lcd_strobe = bool(data & 2);

	// P24: serial tx (TTL)
	m_rs232->write_txd(BIT(data, 4));
}

void svip_state::p5_w(u8 data)
{
	// P50-P53: 4066 control to LCD
	// P56,P57: lcd data
	m_select = data;

	// P54,P55: speaker out
	m_dac->write(data >> 4 & 3);
}

void svip_state::p6_w(u8 data)
{
	// P60-P67: input mux, lcd data
	m_inp_mux = ~data;
}



/*******************************************************************************
    Address Maps
*******************************************************************************/

void svip_state::main_map(address_map &map)
{
	map(0x2000, 0x27ff).mirror(0x1800).ram().share("nvram");
	map(0x4000, 0xbfff).rom().region("eprom", 0);
}



/*******************************************************************************
    Input Ports
*******************************************************************************/

static INPUT_PORTS_START( svip )
	PORT_START("IN.0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_SLASH) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("GO")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_OPENBRACE) PORT_NAME("Restore / Replay")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_NAME("Hint / Human")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_BACKSPACE) PORT_CODE(KEYCODE_DEL) PORT_NAME("C/CB")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_EQUALS) PORT_NAME("Color / Video")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_MINUS) PORT_NAME("Verify/Setup")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_COLON) PORT_NAME("Random / Autoclock")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_N) PORT_NAME("NG")

	PORT_START("IN.1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_COMMA) PORT_NAME("Left / Next Best")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_STOP) PORT_NAME("Right / Autoplay")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_L) PORT_NAME("Pawn / Level")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_K) PORT_NAME("Knight / Info / Echo")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_J) PORT_NAME("Bishop / Easy / Moves")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_O) PORT_NAME("Rook / Solvemate / Baud")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_I) PORT_NAME("Queen / Sound / Game")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_U) PORT_NAME("King / Referee / Board")

	PORT_START("IN.2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_A) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("A 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_B) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("B 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_C) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("C 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_D) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("D 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_E) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("E 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_F) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("F 6")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_G) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("G 7")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_H) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("H 8")

	PORT_START("IN.3")
	PORT_CONFNAME( 0x01, 0x00, "Keyboard Lock")
	PORT_CONFSETTING(    0x00, DEF_STR( Off ) )
	PORT_CONFSETTING(    0x01, DEF_STR( On ) )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_CUSTOM ) PORT_CUSTOM_MEMBER(svip_state, power_r)

	PORT_START("POWER") // needs to be triggered for nvram to work
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_F1) PORT_CHANGED_MEMBER(DEVICE_SELF, svip_state, power_off, 0) PORT_NAME("Power Off")
INPUT_PORTS_END



/*******************************************************************************
    Machine Configs
*******************************************************************************/

void svip_state::svip(machine_config &config)
{
	// basic machine hardware
	HD6301Y0(config, m_maincpu, 9.8304_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &svip_state::main_map);
	m_maincpu->nvram_enable_backup(true);
	m_maincpu->standby_cb().set(m_maincpu, FUNC(hd6301y0_cpu_device::nvram_set_battery));
	m_maincpu->standby_cb().append([this](int state) { if (state) m_lcd_pwm->clear(); });
	m_maincpu->in_p2_cb().set(FUNC(svip_state::p2_r));
	m_maincpu->out_p2_cb().set(FUNC(svip_state::p2_w));
	m_maincpu->out_p5_cb().set(FUNC(svip_state::p5_w));
	m_maincpu->out_p6_cb().set(FUNC(svip_state::p6_w));

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0);

	// video hardware
	PWM_DISPLAY(config, m_lcd_pwm).set_size(4, 10);
	m_lcd_pwm->output_x().set(FUNC(svip_state::lcd_pwm_w));

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_SVG));
	screen.set_refresh_hz(60);
	screen.set_size(1920/2.5, 606/2.5);
	screen.set_visarea_full();

	config.set_default_layout(layout_novag_svip);

	// rs232 (configure after video)
	RS232_PORT(config, m_rs232, default_rs232_devices, nullptr);

	// sound hardware
	SPEAKER(config, "speaker").front_center();
	DAC_2BIT_ONES_COMPLEMENT(config, m_dac).add_route(ALL_OUTPUTS, "speaker", 0.125);
}



/*******************************************************************************
    ROM Definitions
*******************************************************************************/

ROM_START( nsvip ) // serial 7604xx
	ROM_REGION( 0x4000, "maincpu", 0 )
	ROM_LOAD("novag_895_31y0rk75f", 0x0000, 0x4000, CRC(11a3894e) SHA1(fa455181043ac3ee89412aae563171d87ac55596) )

	ROM_REGION( 0x8000, "eprom", 0 ) // ID = V3.7
	ROM_LOAD("yellow", 0x4000, 0x4000, CRC(278e98f0) SHA1(a080e2300c90b6daf42fef960642885eb00ee607) ) // no label
	ROM_CONTINUE(      0x0000, 0x4000 )

	ROM_REGION( 36256, "screen", 0 )
	ROM_LOAD("nvip.svg", 0, 36256, CRC(3373e0d5) SHA1(25bfbf0405017388c30f4529106baccb4723bc6b) )
ROM_END

ROM_START( nsvipa ) // serial 7569xx
	ROM_REGION( 0x4000, "maincpu", 0 )
	ROM_LOAD("novag_895_31y0rk75f", 0x0000, 0x4000, CRC(11a3894e) SHA1(fa455181043ac3ee89412aae563171d87ac55596) )

	ROM_REGION( 0x8000, "eprom", 0 ) // ID = V3.6
	ROM_LOAD("v_117", 0x4000, 0x4000, CRC(2a60a474) SHA1(5dd0a1871fcbbb2a6271f284562a62f98bf9bf93) )
	ROM_CONTINUE(     0x0000, 0x4000 )

	ROM_REGION( 36256, "screen", 0 )
	ROM_LOAD("nvip.svg", 0, 36256, CRC(3373e0d5) SHA1(25bfbf0405017388c30f4529106baccb4723bc6b) )
ROM_END

ROM_START( nsvipb ) // serial 7545xx
	ROM_REGION( 0x4000, "maincpu", 0 )
	ROM_LOAD("novag_895_31y0rk75f", 0x0000, 0x4000, CRC(11a3894e) SHA1(fa455181043ac3ee89412aae563171d87ac55596) )

	ROM_REGION( 0x8000, "eprom", 0 ) // ID = V1.03
	ROM_LOAD("sv_111", 0x4000, 0x4000, CRC(4be9a23f) SHA1(1fb8ccec112e485eedd899cbe8c0c2cb99e840a4) )
	ROM_CONTINUE(      0x0000, 0x4000 )

	ROM_REGION( 36256, "screen", 0 )
	ROM_LOAD("nvip.svg", 0, 36256, CRC(3373e0d5) SHA1(25bfbf0405017388c30f4529106baccb4723bc6b) )
ROM_END

ROM_START( nsvipc ) // serial 7524xx
	ROM_REGION( 0x4000, "maincpu", 0 )
	ROM_LOAD("novag_895_31y0rk75f", 0x0000, 0x4000, CRC(11a3894e) SHA1(fa455181043ac3ee89412aae563171d87ac55596) )

	ROM_REGION( 0x8000, "eprom", 0 ) // ID = V1.01
	ROM_LOAD("s_vip_920", 0x4000, 0x4000, CRC(6f61b1e1) SHA1(df74a599eb59b113ef60c9fc23f9c603a5de4f9e) )
	ROM_CONTINUE(         0x0000, 0x4000 )

	ROM_REGION( 36256, "screen", 0 )
	ROM_LOAD("nvip.svg", 0, 36256, CRC(3373e0d5) SHA1(25bfbf0405017388c30f4529106baccb4723bc6b) )
ROM_END

} // anonymous namespace



/*******************************************************************************
    Drivers
*******************************************************************************/

//    YEAR  NAME    PARENT  COMPAT  MACHINE  INPUT  CLASS       INIT        COMPANY, FULLNAME, FLAGS
SYST( 1989, nsvip,  0,      0,      svip,    svip,  svip_state, empty_init, "Novag", "Super VIP (v3.7)", MACHINE_SUPPORTS_SAVE | MACHINE_CLICKABLE_ARTWORK )
SYST( 1989, nsvipa, nsvip,  0,      svip,    svip,  svip_state, empty_init, "Novag", "Super VIP (v3.6)", MACHINE_SUPPORTS_SAVE | MACHINE_CLICKABLE_ARTWORK )
SYST( 1989, nsvipb, nsvip,  0,      svip,    svip,  svip_state, empty_init, "Novag", "Super VIP (v1.03)", MACHINE_SUPPORTS_SAVE | MACHINE_CLICKABLE_ARTWORK )
SYST( 1989, nsvipc, nsvip,  0,      svip,    svip,  svip_state, empty_init, "Novag", "Super VIP (v1.01)", MACHINE_SUPPORTS_SAVE | MACHINE_CLICKABLE_ARTWORK )
