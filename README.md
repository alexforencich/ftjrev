# ftjev Readme

For more information and updates:
http://alexforencich.com/wiki/en/start

GitHub repository:
https://github.com/alexforencich/ftjrev

## Introduction

ftjrev is a powerful JTAG reverse-engineering tool.  When coupled with an FTDI
based JTAG cable and connected to a target board, ftjrev can be used to
extract a netlist of interconnections between JTAG enabled components. It is
compatible with FT2232 based JTAG cables.  

This version of ftjrev is a modified version of the one released by NSA@home
on this page: http://nsa.unaligned.org/jrev.php .  

## Installation

To build ftjrev, extract and run

    $ make

## ftjrev operations

ftjrev performs four main functions: scanning for clocks, scanning for JTAG
accessible connections, probing inputs, and probing outputs. 

### Clock scanning

Scanning for clocks looks for pins that change without any stimulus. Generally
this is just clock pins, but sometimes other pins will be picked up by clock
scans as well. Clock pins can appear as connected in scans if they are not
identified separately.

#### Example

    $ ./ftjrev clocks
    Found 3 devices with total IR length of 26
    Device 0: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Device 1: IDCODE 21C2E093 (XC3S1200E-FT256)
    Device 2: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Total boundary scan chain: 8572
    Clock pass...
    CLOCK: 0[XC5VLX330-FFG1760]:IO_AN14
    CLOCK: 0[XC5VLX330-FFG1760]:IO_J13
    CLOCK: 0[XC5VLX330-FFG1760]:IO_K13
    CLOCK: 1[XC3S1200E-FT256]:IPAD78
    CLOCK: 1[XC3S1200E-FT256]:K2
    CLOCK: 1[XC3S1200E-FT256]:IPAD258
    CLOCK: 1[XC3S1200E-FT256]:L8
    CLOCK: 2[XC5VLX330-FFG1760]:IO_AM13
    CLOCK: 2[XC5VLX330-FFG1760]:IO_J30
    CLOCK: 2[XC5VLX330-FFG1760]:IO_P37

### Scanning

Scanning for JTAG accessible connections looks for connections between JTAG
pins. It works by setting all of the IO pins as inputs, and then walking a
toggling output around and reading in all the input pins. Pins that are pulled
along with the test output are noted and reported. This method finds most of
the connections between JTAG enabled components, but it cannot identify nets
with only a single JTAG pin nor can it locate what else might be connected to
a given trace besides the JTAG pins.

#### Example

    $ ./ftjrev scan
    Found 3 devices with total IR length of 26
    Device 0: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Device 1: IDCODE 21C2E093 (XC3S1200E-FT256)
    Device 2: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Total boundary scan chain: 8572
    Clock pass...
    CLOCK: 0[XC5VLX330-FFG1760]:IO_AN14
    CLOCK: 0[XC5VLX330-FFG1760]:IO_J13
    CLOCK: 0[XC5VLX330-FFG1760]:IO_K13
    CLOCK: 1[XC3S1200E-FT256]:IPAD78
    CLOCK: 1[XC3S1200E-FT256]:K2
    CLOCK: 1[XC3S1200E-FT256]:IPAD258
    CLOCK: 1[XC3S1200E-FT256]:L8
    CLOCK: 2[XC5VLX330-FFG1760]:IO_AM13
    CLOCK: 2[XC5VLX330-FFG1760]:IO_J30
    CLOCK: 2[XC5VLX330-FFG1760]:IO_P37
    Pin pass...
    0[XC5VLX330-FFG1760]:IO_BB13 --> 2[XC5VLX330-FFG1760]:IO_AT16
    0[XC5VLX330-FFG1760]:IO_AY12 --> 2[XC5VLX330-FFG1760]:IO_AW17
    0[XC5VLX330-FFG1760]:IO_AY13 --> 2[XC5VLX330-FFG1760]:IO_AT20
    0[XC5VLX330-FFG1760]:IO_BA11 --> 2[XC5VLX330-FFG1760]:IO_AT19
    0[XC5VLX330-FFG1760]:IO_BB11 --> 2[XC5VLX330-FFG1760]:IO_AT17
    0[XC5VLX330-FFG1760]:IO_BB12 --> 2[XC5VLX330-FFG1760]:IO_AU16
    0[XC5VLX330-FFG1760]:IO_AW12 --> 2[XC5VLX330-FFG1760]:IO_AW18
    0[XC5VLX330-FFG1760]:IO_AW11 --> 2[XC5VLX330-FFG1760]:IO_AV35
    ....

### Input probing

Input probing does the same thing as scanning, but instead of walking an
output pin around on the board, it toggles a GPIO pin on the FTDI chip in the
JTAG cable. A wire connected to this pin can be used to probe for JTAG
connections on the board. With this mode, conectors and non-JTAG chips can be
probed. However, input probing only works for connections that are not already
being driven by other circuitry, nor does it work for output-only pins.

#### Example:

    $ ./ftjrev iprobe
    Found 3 devices with total IR length of 26
    Device 0: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Device 1: IDCODE 21C2E093 (XC3S1200E-FT256)
    Device 2: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Total boundary scan chain: 8572
    Clock pass...
    CLOCK: 0[XC5VLX330-FFG1760]:IO_AN14
    CLOCK: 0[XC5VLX330-FFG1760]:IO_J13
    CLOCK: 0[XC5VLX330-FFG1760]:IO_K13
    CLOCK: 1[XC3S1200E-FT256]:IPAD78
    CLOCK: 1[XC3S1200E-FT256]:K2
    CLOCK: 1[XC3S1200E-FT256]:IPAD258
    CLOCK: 1[XC3S1200E-FT256]:L8
    CLOCK: 2[XC5VLX330-FFG1760]:IO_AM13
    CLOCK: 2[XC5VLX330-FFG1760]:IO_J30
    CLOCK: 2[XC5VLX330-FFG1760]:IO_P37
    Probing inputs, press ctrl+c to stop...
    0[XC5VLX330-FFG1760]:IO_K20
    0[XC5VLX330-FFG1760]:IO_K20
    0[XC5VLX330-FFG1760]:IO_K20
    0[XC5VLX330-FFG1760]:IO_K20
    0[XC5VLX330-FFG1760]:IO_K20
    0[XC5VLX330-FFG1760]:IO_K20
    ....

### Output probing

Output probing walks a toggling output pin around the board while at the same
time printing the name of the pin to STDOUT. This is not terribly useful in
and of itself, but in addition to an oscilloscope with serial decode
capability, output pins can be traced. The simplest way to set this up is to
pipe the output of ftjrev running an output probe to a serial port, and then
connecting one of the oscilloscope probes to the serial port and enabling
serial decode. Put the oscilloscope in normal trigger mode to trigger on any
edge on a free probe, and then use this probe to browse the board. When the
probe picks up a JTAG triggered edge, the serial decode displayed alongside
will correspond to the connected pin. Sometimes multiple pins will trigger the
same edge; it can take some work to determine the precise cause. Output
probing can sometimes determine what pins driven from external sources are
connected to, but this does not always work. If the non-JTAG device's driver
is weak enough, the JTAG controlled driver may be able to produce enough of a
change in the line to detect on an oscilloscope.

#### Example:

    $ stty -F /dev/ttyUSB2 speed 115200
    $ ./ftjrev oprobe > /dev/ttyUSB2
    Found 3 devices with total IR length of 26
    Device 0: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Device 1: IDCODE 21C2E093 (XC3S1200E-FT256)
    Device 2: IDCODE 2295C093 (XC5VLX330-FFG1760)
    Total boundary scan chain: 8572
    Clock pass...
    Probing outputs, press ctrl+c to stop...

    