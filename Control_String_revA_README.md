# Control_String rev.A — preliminary placement

This is the first physical placement concept, not a fabrication-ready PCB.

## Fixed architecture
- Wemos D1 mini removable on two 1x8 headers.
- Two INA226: addresses 0x44 and 0x45.
- MB85RC256V FRAM, shared I2C.
- Two external shunts; only Kelvin/sense wires enter J_SH1/J_SH2.
- Two independent PV+ voltage-sense dividers, one per channel.
- I2C: SDA GPIO14/D5, SCL GPIO12/D6.

## Important design constraints
- INA226 VBUS range is 0–36 V; typical VBUS input impedance is 830 kOhm (TI datasheet). Divider loading must include this input impedance.
- For 6 x 200 kOhm in series and 68 kOhm lower leg, effective lower leg is 68k || 830k ≈ 62.85k. At 500 V, the VBUS node is approximately 24.9 V nominal.
- Exact resistor package, working-voltage rating, power rating, pulse rating and tolerances remain to be selected.
- HV/LV separation is represented, but creepage/clearance values are not certified.
- USB-connected Wemos ground may connect PV circuitry to a PC/other grounded equipment. Verify the inverter grounding topology before connection.

## Status
- Placement zones and component grouping are drawn.
- No copper routing, drill file, Gerber set, or Sprint-Layout native file is included yet.
- Existing Control_String.lay6 is intentionally untouched.
- This draft is for reviewing arrangement and connector placement only.

## Datasheet references
- TI INA226: https://www.ti.com/lit/ds/symlink/ina226.pdf
- Fujitsu MB85RC256V: https://www.mouser.com/datasheet/2/1113/MB85RC256V_DS501_00017_7v0_E-2329117.pdf
