#!/usr/bin/env python3
"""Generate the coop node BOMs, matching the "Farm IOT Project BOMS" Google Sheet.

  ../bom_amazon.csv  -> sheet tab "Coop Node Carrier PCB": the carrier board's
                        discrete parts, grouped into purchase lines
  ../bom_system.csv  -> sheet tab "Coop Monitor": modules, door hardware and
                        the solar / battery power system for one coop

Columns match the sheet. Where the coop uses the same part as the gate
carrier, the line reuses the product link and pack price already chosen in the
sheet's "Gate Node Carrier PCB" / "Gate Monitor" tabs. New parts have a
product link found on Amazon (price left blank: fill it in from the listing)
or a search link where any listing to the spec will do. The sheet computes
Price Per Unit / Price Per Build / Total Price from these columns.
"""
import csv
import os
from collections import Counter, defaultdict
from urllib.parse import quote_plus

import design

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_BOARD = os.path.abspath(os.path.join(HERE, "..", "bom_amazon.csv"))
OUT_SYSTEM = os.path.abspath(os.path.join(HERE, "..", "bom_system.csv"))

HEADER = ["Component", "Description", "Link", "Required SKU", "Units per SKU",
          "SKU Price", "Price Per Unit", "Required Units", "Price Per Build",
          "Total Price", "Board Refs", "Typical Pack"]


def search(terms):
    return "https://www.amazon.com/s?k=" + quote_plus(terms)


# ---------------------------------------------------------------- board parts
# key -> (component, description, link, units_per_sku, sku_price or None, pack)
# Lines marked "gate" reuse the link and pack price already in the sheet's
# "Gate Node Carrier PCB" tab.
BOARD_LINES = {
    "sock": ("Female pin header 2.54mm",
             "1x40 straight female header strip; cut into 2x 1x18 for the Heltec (cutting loses 1 pin)",
             "https://www.amazon.com/dp/B00VVI1L1W", 10, 5.99, "10-20 strips"),           # gate
    "hdr": ("Male pin header 2.54mm",
            "1x40 breakaway male header; 1x3 for J9 plus 4 single pins to stand the Mini360 off the board",
            "https://www.amazon.com/dp/B0FRZW75VS", 20, 6.29, "10-40 strips"),             # gate
    "jst3": ("JST-XH 3-pin PCB header",
             "JST-XH 2.54mm 3-pin shrouded vertical through-hole header (B3B-XH-A style); kit also includes housings and crimp terminals",
             "https://www.amazon.com/dp/B0B2R99X99", 50, 6.39, "50 headers"),              # gate
    "jst3c": ("JST-XH 3-pin to Dupont cable",
              "20cm 24AWG, JST-XH 3-pin housing to 3x Dupont female, for the AUX relay module",
              "https://www.amazon.com/dp/B0GX9GBVN8", 12, 9.99, "12 cables"),              # gate
    "jst4": ("JST-XH 4-pin PCB header",
             "JST-XH 2.5mm 4-pin vertical through-hole header (B4B-XH-A style), for the H-bridge module",
             search("JST XH 4 pin pcb header B4B-XH-A kit"), None, None, "kit"),
    "jst4c": ("JST-XH 4-pin to Dupont cable",
              "JST-XH 4-pin housing to 4x Dupont female (3V3 / GND / IN1 / IN2) for the H-bridge module; or crimp one from the header kit",
              search("JST XH 4 pin to dupont female cable"), None, None, "10 cables"),
    "tb2": ("Screw terminal 5.08mm 2-pos",
            "PCB screw terminal, 5.08mm pitch, 2 position (KF301-2P / MKDS style)",
            "https://www.amazon.com/dp/B0DPYDRNF7", 60, 7.98, "assortment"),               # gate
    "tb3": ("Screw terminal 5.08mm 3-pos",
            "PCB screw terminal, 5.08mm pitch, 3 position (KF301-3P / MKDS style)",
            "https://www.amazon.com/dp/B0H8SPXGCM", 10, 7.19, "assortment"),               # gate
    "ptc": ("PTC resettable fuse 0.5A",
            "Radial PPTC, 0.5A hold / 1A trip, >=30V (RXEF050, MF-R050 or equivalent), 5mm lead pitch. Logic supply",
            "https://www.amazon.com/dp/B07969FY8X", 10, 5.98, "10-50"),                    # gate
    "ptc3": ("PTC resettable fuse 3A",
             "Radial PPTC, 3A hold / 6A trip, >=16V (RXEF300 30V preferred, MF-RHT300), ~5mm lead pitch. Motor supply",
             search("RXEF300 PPTC resettable fuse 3A radial"), None, None, "10-20"),
    "1n5819": ("Schottky diode 1N5819",
               "1N5819, 40V 1A, DO-41 axial",
               "https://www.amazon.com/dp/B0FC2CWKPL", 150, 7.99, "20-100"),               # gate
    "1n5822": ("Schottky diode 1N5822",
               "1N5822, 40V 3A, DO-201AD axial. Motor supply reverse-polarity protection",
               search("1N5822 schottky diode 3A DO-201AD"), None, None, "10-50"),
    "tvs18": ("TVS diode P6KE18CA",
              "P6KE18CA, 600W bidirectional, 15.3V standoff, DO-15 axial",
              "https://www.amazon.com/dp/B0GX6MC3RH", 20, 4.19, "10-50"),                  # gate
    "tvs68": ("TVS diode P6KE6.8CA",
              "P6KE6.8CA (P6KE6V8CA), 600W bidirectional, 5.8V standoff, DO-15 axial",
              "https://www.amazon.com/dp/B079KJ6LND", 50, 5.99, "10-50"),                  # gate
    "cp100": ("Electrolytic cap 100uF 35V",
              "100uF 35V radial, 6.3mm diameter, 2.5mm lead pitch, 105C (35V covers the 16V positions too)",
              "https://www.amazon.com/dp/B0GTXVDWBK", 50, 5.69, "20-100"),                 # gate
    "c100n": ("Ceramic cap 100nF",
              "100nF (104) 50V ceramic, radial, 5.08mm lead pitch",
              "https://www.amazon.com/dp/B08B3VCK42", 120, 6.99, "50-100"),                 # gate
    "res": ("Resistor kit 1% metal film 1/4W",
            "1/4W 1% metal film, 6.3mm body. Needs 1k, 10k, 22k, 100k; one kit covers every build",
            "https://www.amazon.com/dp/B0FT362FV2", 1, 7.99, "kit"),                       # gate
}


def line_for(ref, value, fp):
    """Purchase line(s) for one board part (a JST position needs header + cable)."""
    v = value.lower()
    if ref in ("J1", "J2"):
        return ["sock"]
    if ref == "J9":
        return ["hdr"]
    if "B4B-XH" in fp:
        return ["jst4", "jst4c"]
    if "JST" in fp:
        return ["jst3", "jst3c"]
    if "MKDS-1,5-2" in fp:
        return ["tb2"]
    if "MKDS-1,5-3" in fp:
        return ["tb3"]
    if ref.startswith("F"):
        return ["ptc3" if "3A" in value else "ptc"]
    if "1n5822" in v:
        return ["1n5822"]
    if "1n5819" in v:
        return ["1n5819"]
    if "p6ke18" in v:
        return ["tvs18"]
    if "p6ke6.8" in v:
        return ["tvs68"]
    if fp.endswith("CP_Radial_D6.3mm_P2.50mm"):
        return ["cp100"]
    if v.startswith("100nf"):
        return ["c100n"]
    if ref.startswith("R"):
        return ["res"]
    if ref.startswith("JP"):
        return []           # solder jumper: copper only
    raise SystemExit(f"no purchasing line for {ref} ({value}, {fp}); add one to BOARD_LINES")


# Parts bought as modules (listed in the system BOM) or not purchased at all
NOT_DISCRETE = {"H1", "H2", "H3", "H4", "U1"}

# ---------------------------------------------------------------- system BOM
# (section, component, description, link, units_per_sku, sku_price, qty, refs, pack)
# Sections become heading rows in the sheet. Prices are filled only where the
# sheet already has them (gate tabs); fill the rest in from the listing.
SYSTEM = [
    ("Electronics", "Heltec WiFi LoRa 32 V3",
     "ESP32-S3 + SX1262 915 MHz LoRa + OLED. Plugs into J1/J2. Same board as the gate node / gateway",
     "https://www.amazon.com/dp/B0CW6151WJ", 2, 44.98, 1, "J1 J2", "2-pack"),
    ("Electronics", "Coop node carrier PCB",
     "110 x 75 mm, 2-layer, 1.6 mm FR-4 (hardware/pcb/coop_node_carrier). Order Gerbers from JLCPCB / PCBWay; parts on the 'Coop Node Carrier PCB' tab",
     "https://cart.jlcpcb.com/quote", 5, None, 1, "", "5 boards"),
    ("Electronics", "Antenna pigtail U.FL to SMA",
     "U.FL (IPEX) to RP-SMA female bulkhead, 6 inch, so the antenna mounts outside the enclosure",
     "https://www.amazon.com/dp/B07T85B3LN", 2, 6.39, 1, "", "2 pcs"),
    ("Electronics", "915 MHz LoRa antenna",
     "915 MHz omni, 6 dBi, SMA male. Mount vertically, outside, towards the house",
     "https://www.amazon.com/dp/B0FQK27BSP", 2, 12.99, 1, "", "2 pcs"),
    ("Electronics", "Mini360 buck converter (5V)",
     "MP1584EN 5-30V in, set to 5V out. Mounts on the carrier at U1 on 4 header pins",
     "https://www.amazon.com/dp/B0FQTP5HGZ", 5, 9.57, 1, "U1", "5-pack"),
    ("Electronics", "DHT22 temp + humidity sensor",
     "DHT22 / AM2302 module (3-pin, has its own pull-up, so R7 can be left out). J8 + / DATA / -. Keep the cable ~1 m at 3.3V",
     "https://www.amazon.com/dp/B0D9W5CMHS", 3, 7.99, 1, "J8", "3-pack"),
    ("Electronics", "Relay module (AUX, optional)",
     "1-channel 3V relay module for a light / heater on J5 (firmware holds it off for now)",
     "https://www.amazon.com/dp/B09XTZSG53", 6, 9.99, 1, "J5", "6-pack"),
    ("Electronics", "Weatherproof enclosure",
     "IP65 ABS junction box, about 200 x 120 x 75 mm, room for the carrier + 25 mm above it for the Heltec and cables; cable glands",
     search("IP65 waterproof junction box 200x120x75 with cable glands"), None, None, 1, "", "1"),

    ("Door", "H-bridge motor driver (DRV8871)",
     "DRV8871 breakout, 6.5-45V, 3.6A peak, IN1/IN2 logic. Power from J10 (VM/GND), logic from J3 (GND/IN1/IN2)",
     "https://www.amazon.com/dp/B0CDWW7FZ8", 2, None, 1, "J3 J10", "2-pack"),
    ("Door", "12V worm gear motor",
     "12V DC worm gear motor, ~35 RPM, self-locking (the door stays put with the power off). With a ~25 mm spool a 30 cm door lifts in ~7 s",
     "https://www.amazon.com/dp/B00NYP2W54", 1, None, 1, "H-bridge OUT", "1"),
    ("Door", "Limit switches (open / closed)",
     "2x N.O. magnetic reed switches with magnets, wired, outdoor rated. Each closes to GND at its end of travel",
     search("magnetic reed switch normally open wired door sensor 2 pack"), None, None, 2, "J7", "2-10"),
    ("Door", "Door push button (optional)",
     "Momentary N.O. push button, IP67, 16-19 mm panel mount. Open / close / stop at the coop",
     search("momentary push button waterproof IP67 normally open 16mm"), None, None, 1, "J11", "1-5"),
    ("Door", "Door rope / pulley hardware",
     "Braided line + small pulley for a vertical slide (guillotine) door; the door closes by gravity",
     search("small pulley wheel and braided cord diy"), None, None, 1, "", "kit"),

    ("Solar power", "Battery 12V 10Ah LiFePO4",
     "12V 10Ah LiFePO4 with a BMS that BLOCKS CHARGING BELOW 0C (check the listing). ~120 Wh: ~12 days of node standby. "
     "Alternative for very cold coops: 12V 12Ah AGM (charges below freezing; use half its capacity)",
     search("12V 10Ah LiFePO4 battery low temperature charge protection"), None, None, 1, "", "1"),
    ("Solar power", "Solar panel 20W 12V",
     "20W 12V monocrystalline (Voc ~21-22V). Covers the node (~8 Wh/day) plus recharge after cloudy days in winter sun",
     "https://www.amazon.com/dp/B00W813E4I", 1, None, 1, "", "1"),
    ("Solar power", "Solar charge controller 10A PWM",
     "10A 12V PWM controller with a LiFePO4 profile, LOAD terminals and adjustable low-voltage disconnect. Carrier J6 goes on the LOAD output",
     "https://www.amazon.com/dp/B09MV5ZX59", 1, None, 1, "J6", "1"),
    ("Solar power", "Inline fuse holder + 5A fuse",
     "ATC blade fuse holder, 14-16 AWG, with 5A fuse, at the battery positive (battery -> controller)",
     search("inline ATC blade fuse holder 14 awg waterproof 5A"), None, None, 1, "", "pack"),
    ("Solar power", "Wire 16 AWG + MC4 extension",
     "Red/black 16 AWG for battery and load runs; MC4 extension from the panel to the controller",
     search("16 awg red black wire 25 ft"), None, None, 1, "", "spool"),
]


def board_rows():
    groups = defaultdict(list)
    for ref, (_, value, fp, _, _, _) in design.PARTS.items():
        if ref in NOT_DISCRETE:
            continue
        for key in line_for(ref, value, fp):
            groups[key].append(ref)

    def ref_key(r):
        p = r.rstrip("0123456789")
        return (p, int(r[len(p):]))

    one_per_build = {"sock", "hdr", "res"}
    rows = []
    for key, (comp, desc, link, units, price, pack) in BOARD_LINES.items():
        refs = sorted(groups.get(key, []), key=ref_key)
        if not refs:
            continue
        if key == "res":
            counts = Counter(design.PARTS[r][1].split()[0] for r in refs)
            desc += " (" + ", ".join(f"{n}x {v}" for v, n in sorted(counts.items())) + ")"
        qty = 1 if key in one_per_build else len(refs)
        rows.append([comp, desc, link, 1, units or "", "" if price is None else price, "",
                     qty, "", "", " ".join(refs), pack])
    return rows


def system_rows():
    rows = []
    section = None
    for sec, comp, desc, link, units, price, qty, refs, pack in SYSTEM:
        if sec != section:
            rows.append([sec] + [""] * (len(HEADER) - 1))
            section = sec
        rows.append([comp, desc, link, 1, units or "", "" if price is None else price, "",
                     qty, "", "", refs, pack])
    return rows


def write(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(HEADER)
        w.writerows(rows)


def main():
    rows = board_rows()
    write(OUT_BOARD, rows)
    print(f"wrote {os.path.relpath(OUT_BOARD)}: {len(rows)} board purchase lines")
    srows = system_rows()
    write(OUT_SYSTEM, srows)
    print(f"wrote {os.path.relpath(OUT_SYSTEM)}: {sum(1 for r in srows if r[2])} system lines")


if __name__ == "__main__":
    main()
