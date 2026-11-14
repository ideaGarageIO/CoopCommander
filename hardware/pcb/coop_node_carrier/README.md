# Coop Node Carrier Board (rev A)

Interconnect board for the coop node: a Heltec WiFi LoRa 32 V3 plugs into
female headers, and every off-board module and field wire lands on a
connector. 110 x 75 mm, 2 layers, all through-hole, M3 holes in the corners.

Derived from GateMonitor's `gate_node_carrier`: the 12 V input chain, battery
sense, limit-switch conditioning, DHT22 terminal and Heltec footprint are the
same circuit (and the same verified Heltec geometry). The coop board swaps the
two gate relays for an H-bridge motor connection with its own fused supply,
keeps one relay channel as AUX, and adds an external button input.

KiCad 6 project, **generated** from `scripts/design.py`:

```bash
cd hardware/pcb/coop_node_carrier/scripts
python3 gen_schematic.py     # -> coop_node_carrier.kicad_sch
python3 gen_pcb.py --check   # placement sanity check only
python3 gen_pcb.py           # -> coop_node_carrier.kicad_pcb (+ net classes in .kicad_pro)
python3 gen_bom.py           # -> bom_amazon.csv (board parts + off-board modules)
python3 gen_3d.py            # -> coop_node.3dshapes/ placeholder models
```

`gen_pcb.py` needs KiCad's `pcbnew` Python module (`sudo apt install kicad`).
Regenerating the PCB **overwrites routing**: once you route in KiCad, treat
the `.kicad_pcb` as hand-edited and use *Tools → Update PCB from Schematic*.

## Block diagram

```
12V battery ─ J6 ─┬─ F1 0.5A ─ D1 1N5819 ─ VIN ─┬─ U1 Mini360 ─ D3 ─ +5V ─ Heltec 5V
                  │                             ├─ D2 P6KE18CA, C1 100µF, C2 100nF
                  │  (BATT_F) R1 100k / R2 22k ─ BATT_ADC (GPIO2)
                  │
                  └─ F2 3A ─ D6 1N5822 ─ MOTOR_VM ─┬─ C8 100µF ─ J10 ─► H-bridge VM/GND
                                                   (1.5 mm "Motor" net class)
Heltec 3V3 ─┬─ J3 MOTOR CTRL (3V3 / GND / IN1 GPIO7 / IN2 GPIO4, 10k pull-downs R10/R11)
            ├─ R3/R5 10k ─ J7 limit switches (1k + 100nF, P6KE6.8CA) ─ GPIO5 / GPIO6
            ├─ R8 10k ─ J11 button (1k + 100nF, P6KE6.8CA) ─ GPIO33
            ├─ JP1 (1-2) ─ RELAY_VCC ─ J5 AUX relay module (IN = GPIO47)
            └─ J8 DHT22 (+ R7 10k) GPIO48, J9 expansion (GPIO34)
```

## Pin map (firmware must match)

| Net | GPIO | Connector |
|---|---|---|
| LIM_OPEN | 5 | J7-1 |
| LIM_CLOSED | 6 | J7-2 |
| MOTOR_IN1 | 7 | J3-3 |
| MOTOR_IN2 | 4 | J3-4 |
| AUX_RELAY | 47 | J5-3 |
| BATT_ADC | 2 | R1/R2 divider (ratio 5.545) |
| DHT_DATA | 48 | J8-2 |
| BTN_EXT | 33 | J11-1 (via R9) |
| EXP_IO34 | 34 | J9-2 |

## Connectors

| Ref | Label | Pins | To |
|---|---|---|---|
| J6 | 12V BATT | 12V+ / GND | Battery (through the charge controller's load output) |
| J10 | DRIVER PWR | VM / GND | H-bridge module motor supply |
| J3 | MOTOR CTRL | 3V3 / GND / IN1 / IN2 | H-bridge module logic (JST-XH 4) |
| J8 | DHT22 | + / DATA / - | DHT22 |
| J7 | LIMITS | OPEN / CLOSED / GND | Two dry-contact limit switches |
| J11 | BUTTON | BTN / GND | Momentary push button |
| J5 | AUX RELAY | DC+ / DC- / IN | Relay module (JST-XH 3) |
| J9 | EXPANSION | 3V3 / IO34 / GND | Spare |

The motor itself wires to the H-bridge module's output terminals, not to this
board.

## BOM

`python3 scripts/gen_bom.py` writes two CSVs whose columns match the
"Farm IOT Project BOMS" Google Sheet:

- `bom_amazon.csv` → tab **Coop Node Carrier PCB**: the board's parts grouped
  into purchase lines. Parts shared with the gate carrier reuse the product
  links and pack prices already chosen for it.
- `bom_system.csv` → tab **Coop Monitor**: Heltec, antenna, Mini360, DHT22,
  enclosure, door motor + DRV8871 driver, limit switches, button, and the
  solar system (20 W panel, 12 V 10 Ah LiFePO4, 10 A PWM controller, fuse).
  Sizing in [docs/power.md](../../../docs/power.md).

New parts have a blank SKU price: fill it in from the listing in the sheet.

## VERIFY before ordering boards

1. **Heltec geometry and pin order**: same as the gate carrier, which was
   checked against Heltec's datasheet. Still caliper-check one board.
2. **MF-RHT300 footprint / lead pitch**: the KiCad footprint is Bourns
   MF-RHT300. An RXEF300 (30 V) is a better voltage match for a charging 12 V
   system; check its lead pitch fits the footprint, or pick its footprint.
3. **1N5822 lead bend**: DO-201AD with 15.24 mm pitch. Check your diodes' body
   length.
4. **H-bridge module wiring**: J3 assumes 3V3 / GND / IN1 / IN2. Crimp the
   cable to match your module's header (a DRV8871 breakout needs only GND /
   IN1 / IN2; leave 3V3 unconnected).
5. **Motor current**: F2 holds 3 A and D6 is a 3 A diode. A motor that stalls
   above ~3 A will trip F2. Measure the stall current of your motor.
6. **Limit switches and button must be dry contacts to GND.**
7. Open the schematic and run **ERC**. Open the PCB, route it, run **DRC**, and
   check the parts in the 3D viewer.

## Notes

- D6 drops ~0.5 V at 2 A (~1 W). Leave space around it; don't bury it under the
  Heltec.
- The 1N5819 + 0.5 A fuse logic path is kept separate from the motor path, so
  a stalled motor trips F2 without browning out the ESP32.
- Mount the DHT22 inside the coop away from the heat lamp, or under the eaves
  outside, using the J8 terminal.
