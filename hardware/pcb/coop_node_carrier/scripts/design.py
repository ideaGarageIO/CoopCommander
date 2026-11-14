"""
Single source of truth for the Coop Node carrier board.

Derived from GateMonitor's gate_node_carrier: same Heltec socket, 12 V input
chain, battery sense, limit-switch conditioning and DHT22 terminal. The coop
version replaces the two gate relays with a motor-driver connection (an
off-board H-bridge module such as a DRV8871 breakout, fed from its own 3 A
fused supply), keeps one relay channel as AUX (light / heater), and adds an
external push-button input.

Both gen_schematic.py and gen_pcb.py read this file, so the schematic and
the PCB are always generated from the same netlist and reference designators.

Edit parts / nets / placement here, then re-run:
    python3 scripts/gen_schematic.py
    python3 scripts/gen_pcb.py        (needs KiCad's pcbnew python module)

Anything marked VERIFY is a mechanical dimension or pin order taken from
memory / typical module listings and must be checked with calipers against
the actual parts before ordering boards.
"""

# --------------------------------------------------------------------------
# Board
# --------------------------------------------------------------------------
BOARD_W = 110.0          # mm
BOARD_H = 75.0           # mm
ORIGIN = (100.0, 100.0)  # top-left corner in KiCad page coordinates

# Heltec WiFi LoRa 32 V3 header geometry, measured from the vector drawing in
# the official datasheet (HTIT-WB32LA_V3.2.pdf, section 4 "Physical dimensions"):
#   board 25.52 x 50.2 mm, 2x18 pins at 2.54 mm, rows centred on the board,
#   row spacing 23.04 mm, pin 1 is 3.36 mm from the USB-C face / 2.70 mm from
#   the PCB edge. Caliper-check on a real board before ordering.
HELTEC_ROW_SPACING = 23.04   # mm between the two 1x18 header rows
HELTEC_USB_FACE_X = 100.6    # x of the USB-C connector face (left board edge = 100)
HELTEC_PIN1_X = HELTEC_USB_FACE_X + 3.36   # pin 1 (GND) is at the USB-C end
HELTEC_TOP_ROW_Y = 110.0     # y of the Heltec "J3" row (3V3 / GPIO1-7)

# --------------------------------------------------------------------------
# GPIO map (firmware/coop_node/coop_node.ino must match)
#   Avoid: 8-14 (SX1262 LoRa), 17/18/21 (OLED), 0 (PRG button), 1 (Heltec
#   VBAT divider), 3/45/46 (strapping), 19/20 (USB), 35 (LED), 36 (Vext).
# --------------------------------------------------------------------------
GPIO = {
    "LIM_OPEN": 5,
    "LIM_CLOSED": 6,
    "MOTOR_IN1": 7,
    "MOTOR_IN2": 4,
    "AUX_RELAY": 47,
    "BATT_ADC": 2,
    "DHT_DATA": 48,
    "BTN_EXT": 33,
    "EXP_IO34": 34,
}

# Heltec V3 header pin order, pin 1 (GND) at the USB-C end.
# Source: datasheet tables 2.2-1 / 2.2-2 and heltec.org pin map image.
# Heltec "J3" (3V3 / GPIO1-7). Top row when OLED faces up and USB-C points left.
HELTEC_J_GPIO = ["GND", "3V3", "3V3", "37", "46", "45", "42", "41", "40",
                 "39", "38", "1", "2", "3", "4", "5", "6", "7"]
# Heltec "J2" (5V / GPIO19-48). Bottom row.
HELTEC_J_5V = ["GND", "5V", "Ve", "Ve", "44", "43", "RST", "0", "36", "35",
               "34", "33", "47", "48", "26", "21", "20", "19"]


def _heltec_pins(names):
    gpio_to_net = {str(v): k for k, v in GPIO.items()}
    pins = {}
    for i, name in enumerate(names, start=1):
        if name == "GND":
            net = "GND"
        elif name == "3V3":
            net = "+3V3"
        elif name == "5V":
            net = "+5V"
        else:
            net = gpio_to_net.get(name)  # None -> no-connect
        pins[str(i)] = net
    return pins


# --------------------------------------------------------------------------
# Footprint shorthands
# --------------------------------------------------------------------------
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
FP_C = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
FP_CP = "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm"
FP_D41 = "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal"
FP_D15 = "Diode_THT:D_DO-15_P10.16mm_Horizontal"
FP_TB2 = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal"
FP_TB3 = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal"
FP_JST3 = "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical"
FP_JST4 = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
FP_D201 = "Diode_THT:D_DO-201AD_P15.24mm_Horizontal"
FP_PTC3 = "Fuse:Fuse_Bourns_MF-RHT300"
FP_HDR3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
FP_SOCK18 = "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical"
FP_HOLE = "MountingHole:MountingHole_3.2mm_M3"
FP_PTC = "Fuse:Fuse_Bourns_MF-RHT050"
FP_JP3 = "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm"
FP_MINI360 = "coop_node:Mini360_MP1584"

# --------------------------------------------------------------------------
# Parts
#   ref: (lib_symbol, value, footprint, {pin: net}, group, description)
# Pin numbers are the KiCad symbol / footprint pad numbers.
#   Diodes: 1 = K, 2 = A.  C_Polarized: 1 = +.  SolderJumper_3: 2 = centre.
# --------------------------------------------------------------------------
PARTS = {
    # ---- Heltec module ----------------------------------------------------
    "J1": ("Connector_Generic:Conn_01x18", "Heltec_V3_J3 (3V3/GPIO1-7)", FP_SOCK18,
           _heltec_pins(HELTEC_J_GPIO), "mcu", "Heltec V3 header, 3V3/GPIO side"),
    "J2": ("Connector_Generic:Conn_01x18", "Heltec_V3_J2 (5V/GPIO19-48)", FP_SOCK18,
           _heltec_pins(HELTEC_J_5V), "mcu", "Heltec V3 header, 5V side"),

    # ---- 12 V input & protection -----------------------------------------
    "J6": ("Connector:Screw_Terminal_01x02", "12V BATT", FP_TB2,
           {"1": "BATT_IN", "2": "GND"}, "power", "Battery input"),
    "F1": ("Device:Polyfuse", "MF-RHT050 0.5A", FP_PTC,
           {"1": "BATT_IN", "2": "BATT_F"}, "power", "Resettable fuse"),
    "D1": ("Device:D_Schottky", "1N5819", FP_D41,
           {"1": "VIN", "2": "BATT_F"}, "power", "Reverse-polarity protection"),
    "D2": ("Device:D_TVS", "P6KE18CA", FP_D15,
           {"1": "VIN", "2": "GND"}, "power", "Input surge clamp"),
    "C1": ("Device:C_Polarized", "100uF 35V", FP_CP,
           {"1": "VIN", "2": "GND"}, "power", "Input bulk"),
    "C2": ("Device:C", "100nF", FP_C,
           {"1": "VIN", "2": "GND"}, "power", "Input decoupling"),
    "U1": ("Connector_Generic:Conn_01x04", "Mini360 MP1584 5V", FP_MINI360,
           {"1": "VIN", "2": "GND", "3": "GND", "4": "5V_BUCK"}, "power",
           "Buck module: 1=IN+ 2=IN- 3=OUT- 4=OUT+"),
    "D3": ("Device:D_Schottky", "1N5819", FP_D41,
           {"1": "+5V", "2": "5V_BUCK"}, "power", "Blocks USB back-feed into buck"),
    "C3": ("Device:C_Polarized", "100uF 16V", FP_CP,
           {"1": "+5V", "2": "GND"}, "power", "5V bulk"),

    # ---- Battery sense ----------------------------------------------------
    "R1": ("Device:R", "100k 1%", FP_R,
           {"1": "BATT_F", "2": "BATT_ADC"}, "sense", "Divider top"),
    "R2": ("Device:R", "22k 1%", FP_R,
           {"1": "BATT_ADC", "2": "GND"}, "sense", "Divider bottom (ratio 5.545)"),
    "C4": ("Device:C", "100nF", FP_C,
           {"1": "BATT_ADC", "2": "GND"}, "sense", "ADC filter"),

    # ---- Limit switch inputs (dry contact to GND) -------------------------
    "J7": ("Connector:Screw_Terminal_01x03", "LIMITS", FP_TB3,
           {"1": "LIM_OPEN_EXT", "2": "LIM_CLOSED_EXT", "3": "GND"}, "inputs",
           "Limit switches"),
    "R3": ("Device:R", "10k", FP_R,
           {"1": "+3V3", "2": "LIM_OPEN_EXT"}, "inputs", "Pull-up / wetting"),
    "R4": ("Device:R", "1k", FP_R,
           {"1": "LIM_OPEN_EXT", "2": "LIM_OPEN"}, "inputs", "Series protection"),
    "C5": ("Device:C", "100nF", FP_C,
           {"1": "LIM_OPEN", "2": "GND"}, "inputs", "Debounce (tau=0.1ms)"),
    "D4": ("Device:D_TVS", "P6KE6.8CA", FP_D15,
           {"1": "LIM_OPEN_EXT", "2": "GND"}, "inputs", "Field-wire surge clamp"),
    "R5": ("Device:R", "10k", FP_R,
           {"1": "+3V3", "2": "LIM_CLOSED_EXT"}, "inputs", "Pull-up / wetting"),
    "R6": ("Device:R", "1k", FP_R,
           {"1": "LIM_CLOSED_EXT", "2": "LIM_CLOSED"}, "inputs", "Series protection"),
    "C6": ("Device:C", "100nF", FP_C,
           {"1": "LIM_CLOSED", "2": "GND"}, "inputs", "Debounce"),
    "D5": ("Device:D_TVS", "P6KE6.8CA", FP_D15,
           {"1": "LIM_CLOSED_EXT", "2": "GND"}, "inputs", "Field-wire surge clamp"),

    # ---- Motor supply (separate 3 A fused path to the H-bridge module) -----
    "F2": ("Device:Polyfuse", "MF-RHT300 3A", FP_PTC3,
           {"1": "BATT_IN", "2": "BATT_MF"}, "motor", "Motor supply fuse (3 A hold)"),
    "D6": ("Device:D_Schottky", "1N5822", FP_D201,
           {"1": "MOTOR_VM", "2": "BATT_MF"}, "motor", "Motor supply reverse-polarity protection (3 A)"),
    "C8": ("Device:C_Polarized", "100uF 35V", FP_CP,
           {"1": "MOTOR_VM", "2": "GND"}, "motor", "Motor supply bulk"),
    "J10": ("Connector:Screw_Terminal_01x02", "DRIVER PWR", FP_TB2,
            {"1": "MOTOR_VM", "2": "GND"}, "motor", "To H-bridge module VM / GND"),

    # ---- Motor control (off-board H-bridge, JST-XH cable) -----------------
    "J3": ("Connector_Generic:Conn_01x04", "MOTOR CTRL", FP_JST4,
           {"1": "+3V3", "2": "GND", "3": "MOTOR_IN1", "4": "MOTOR_IN2"}, "motor",
           "To H-bridge module VCC / GND / IN1 / IN2"),
    "R10": ("Device:R", "10k", FP_R,
            {"1": "MOTOR_IN1", "2": "GND"}, "motor", "Hold IN1 low while the ESP32 resets"),
    "R11": ("Device:R", "10k", FP_R,
            {"1": "MOTOR_IN2", "2": "GND"}, "motor", "Hold IN2 low while the ESP32 resets"),

    # ---- AUX relay (off-board module: light / heater) ---------------------
    "JP1": ("Jumper:SolderJumper_3_Open", "RELAY_VCC 1-2=3V3 2-3=5V", FP_JP3,
            {"1": "+3V3", "2": "RELAY_VCC", "3": "+5V"}, "aux",
            "Bridge 1-2 for 3 V relay modules, 2-3 for 5 V"),
    "C7": ("Device:C_Polarized", "100uF 16V", FP_CP,
           {"1": "RELAY_VCC", "2": "GND"}, "aux", "Relay coil inrush"),
    "J5": ("Connector_Generic:Conn_01x03", "AUX RELAY", FP_JST3,
           {"1": "RELAY_VCC", "2": "GND", "3": "AUX_RELAY"}, "aux",
           "To relay module DC+ / DC- / IN"),

    # ---- External button (dry contact to GND) ------------------------------
    "J11": ("Connector:Screw_Terminal_01x02", "BUTTON", FP_TB2,
            {"1": "BTN_EXT_EXT", "2": "GND"}, "inputs", "Door push button"),
    "R8": ("Device:R", "10k", FP_R,
           {"1": "+3V3", "2": "BTN_EXT_EXT"}, "inputs", "Pull-up / wetting"),
    "R9": ("Device:R", "1k", FP_R,
           {"1": "BTN_EXT_EXT", "2": "BTN_EXT"}, "inputs", "Series protection"),
    "C9": ("Device:C", "100nF", FP_C,
           {"1": "BTN_EXT", "2": "GND"}, "inputs", "Debounce"),
    "D7": ("Device:D_TVS", "P6KE6.8CA", FP_D15,
           {"1": "BTN_EXT_EXT", "2": "GND"}, "inputs", "Field-wire surge clamp"),

    # ---- DHT22 + expansion ------------------------------------------------
    "J8": ("Connector:Screw_Terminal_01x03", "DHT22", FP_TB3,
           {"1": "+3V3", "2": "DHT_DATA", "3": "GND"}, "aux", "DHT22 + / DATA / -"),
    "R7": ("Device:R", "10k", FP_R,
           {"1": "+3V3", "2": "DHT_DATA"}, "aux", "DHT pull-up (DNP if module has one)"),
    "J9": ("Connector_Generic:Conn_01x03", "EXPANSION", FP_HDR3,
           {"1": "+3V3", "2": "EXP_IO34", "3": "GND"}, "aux", "Spare GPIO header"),

    # ---- Mechanical -------------------------------------------------------
    "H1": ("Mechanical:MountingHole", "M3", FP_HOLE, {}, "mech", ""),
    "H2": ("Mechanical:MountingHole", "M3", FP_HOLE, {}, "mech", ""),
    "H3": ("Mechanical:MountingHole", "M3", FP_HOLE, {}, "mech", ""),
    "H4": ("Mechanical:MountingHole", "M3", FP_HOLE, {}, "mech", ""),
}

POWER_NETS = {"BATT_F", "VIN", "5V_BUCK", "+5V", "+3V3", "RELAY_VCC", "GND"}
# Motor current (up to ~3 A) flows J6 -> F2 -> D6 -> J10 and back on GND
MOTOR_NETS = {"BATT_IN", "BATT_MF", "MOTOR_VM"}

# --------------------------------------------------------------------------
# PCB placement: ref -> (x, y, rotation_deg). Absolute KiCad mm.
# --------------------------------------------------------------------------
_x0, _y0 = ORIGIN
PLACEMENT = {
    # Heltec: rotation 90 runs pins 1..18 left to right, pin 1 at the USB-C end
    "J1": (HELTEC_PIN1_X, HELTEC_TOP_ROW_Y, 90),
    "J2": (HELTEC_PIN1_X, HELTEC_TOP_ROW_Y + HELTEC_ROW_SPACING, 90),

    # Field-wiring terminals along the bottom edge, wire entry facing the edge
    "J6": (112.0, 169.0, 0),     # 12V battery
    "J10": (124.0, 169.0, 0),    # H-bridge module supply
    "J8": (136.0, 169.0, 0),     # DHT22
    "J7": (153.0, 169.0, 0),     # limit switches
    "J11": (170.0, 169.0, 0),    # push button

    # 12V logic input chain (left)
    "F1": (104.0, 140.0, 0),
    "D1": (113.0, 140.0, 0),
    "C1": (129.0, 140.0, 0),
    "C2": (136.0, 140.0, 0),
    "U1": (112.0, 150.0, 0),
    "D2": (123.0, 147.0, 0),
    "D3": (123.0, 153.0, 0),
    "C3": (138.0, 152.0, 0),

    # Motor supply chain (left, above the terminals)
    "F2": (103.0, 159.0, 0),
    "D6": (113.0, 159.0, 0),
    "C8": (134.0, 159.0, 0),

    # Battery sense + DHT pull-up (centre)
    "R1": (146.0, 140.0, 0),
    "R2": (146.0, 145.0, 0),
    "C4": (146.0, 150.0, 0),
    "R7": (146.0, 155.0, 0),

    # Limit-switch + button conditioning (bottom right, above J7/J11)
    "R3": (158.0, 138.0, 0),
    "R4": (170.0, 138.0, 0),
    "C5": (182.0, 138.0, 0),
    "R5": (158.0, 143.0, 0),
    "R6": (170.0, 143.0, 0),
    "C6": (182.0, 143.0, 0),
    "D4": (158.0, 149.0, 0),
    "D5": (174.0, 149.0, 0),
    "D7": (190.0, 149.0, 0),
    "R8": (158.0, 155.0, 0),
    "R9": (170.0, 155.0, 0),
    "C9": (182.0, 155.0, 0),

    # Motor control + AUX relay (top right)
    "J9": (160.0, 108.0, 0),
    "R10": (158.0, 126.0, 0),
    "R11": (158.0, 131.0, 0),
    "JP1": (174.0, 112.0, 0),
    "C7": (174.0, 122.0, 0),
    "J3": (190.0, 108.0, 0),
    "J5": (190.0, 121.0, 0),

    # Mounting holes
    "H1": (104.0, 104.0, 0),
    "H2": (206.0, 104.0, 0),
    "H3": (104.0, 171.0, 0),
    "H4": (206.0, 171.0, 0),
}

# Silkscreen helper labels: (text, x, y, size_mm)
SILK_TEXT = [
    ("COOP NODE CARRIER rev A", 192.0, 162.0, 1.2),
    ("12V+  GND", 114.5, 163.0, 0.9),
    ("VM  GND", 126.5, 163.0, 0.9),
    ("+  DATA  -", 141.0, 163.0, 0.9),
    ("OPEN CLOSED GND", 158.0, 163.0, 0.9),
    ("BTN  GND", 172.5, 163.0, 0.9),
    ("3V3 GND IN1 IN2", 194.0, 103.5, 0.9),
    ("MOTOR", 205.0, 113.5, 0.9),
    ("DC+ DC- IN", 192.5, 116.5, 0.9),
    ("AUX", 203.0, 126.5, 0.9),
    ("RLY: 1-2=3V3 2-3=5V", 174.0, 108.5, 0.8),
    ("3V3 IO34 GND", 160.0, 117.0, 0.8),
    ("HELTEC V3  (OLED UP, USB-C <)", 128.0, 121.4, 1.2),
]

# Heltec module outline for silkscreen (50.2 x 25.5 mm incl. USB-C overhang)
HELTEC_OUTLINE = (HELTEC_USB_FACE_X, HELTEC_TOP_ROW_Y + HELTEC_ROW_SPACING / 2 - 12.75,
                  HELTEC_USB_FACE_X + 50.2, HELTEC_TOP_ROW_Y + HELTEC_ROW_SPACING / 2 + 12.75)
