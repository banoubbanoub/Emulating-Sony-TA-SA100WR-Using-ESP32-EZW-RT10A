Emulating Sony TA-SA100WR Receiver
Using ESP32 + EZW-RT10A to Capture Sony S-AIR Wireless Audio
Overview

This project demonstrates how to emulate the Sony TA-SA100WR wireless surround receiver using an ESP32 and a real EZW-RT10A module.

The system captures Sony’s proprietary S-AIR wireless audio, extracts the digital I2S stream, converts it in real time, and outputs 
it to an external amplifier such as a Samsung TV board (TAS5749M).
What This Project Does
✅ Emulates Sony TA-SA100WR host behavior
✅ Controls EZW-RT10A over I2C
✅ Pairs with Sony S-AIR transmitter
✅ Receives wireless audio stream
✅ Captures Sony I2S output
✅ Converts I2S format in real time
✅ Outputs audio to external amplifier (TAS5749M)

System Architecture
Sony Transmitter (S-AIR)
        │
        ▼
EZW-RT10A (Wireless Receiver Module)
        │ I2S (Sony format)
        ▼
ESP32 (Receiver Emulator + Converter)
        │ I2S (converted format)
        ▼
Samsung TV Board (TAS5749M)
        │
        ▼
Speakers

Hardware Used
ESP32 (main controller)
Sony EZW-RT10A module
Samsung TV audio board (TAS5749M)
Logic analyzer (for reverse engineering)
Power supply (3.3V stable)
How It Works
 1. EZW-RT10A Receives Wireless Audio
Pairs with Sony transmitter
Handles RF + DSP internally
Outputs digital audio via I2S
2. ESP32 Emulates Sony Receiver

The ESP32 replaces the Sony TA-SA100WR logic by:

controlling EZW-RT10A via I2C
handling pairing behavior
maintaining communication state
monitoring LINK/INT signals

3. I2S Audio Capture

ESP32 reads I2S from EZW-RT10A:

Sample rate: 48 kHz
Format: 32-bit stereo
BCK: ~3.072 MHz
4. Real-Time I2S Conversion

Sony format is not directly compatible with most amplifiers.

ESP32 converts:

Stereo → Mono (or adjusted format)
32-bit → usable format
Re-generates clocks
5. Audio Output (TAS5749M)

The converted I2S stream is sent to:

Samsung TV board (TAS5749M)
Amplified and played through speakers

I2S Format Challenge
Sony Output:
48 kHz
32-bit stereo
BCK ≈ 3.072 MHz
External Amplifier:
Requires lower BCK (~1.5 MHz)
Different framing

 Direct connection does not work

 Solution

ESP32 acts as an I2S bridge + converter:

Receives Sony I2S (slave mode)
Processes audio
Outputs compatible I2S (master mode)
Key Insights
Sony systems require correct behavior, not just signals
I2S compatibility depends on:
bit depth
framing
clock relationships
Wireless audio is locked behind proper initialization
Challenges
Pairing requires correct I2C timing
LINK signal must be stable
Audio may be present but unusable due to format mismatch
TAS5749M requires specific configuration to work
Results
✅ Successful pairing with Sony transmitter
✅ Stable wireless audio reception
✅ Real-time audio conversion
✅ External audio playback achieved
Repository Contents
ESP32 receiver emulator code
I2S conversion implementation
TAS5749M initialization code
Supporting headers and utilities
Disclaimer

This project is provided for educational and research purposes only.

Use this code at your own risk. The author is not responsible for:

hardware damage
incorrect usage
unintended behavior
uture Work
Stereo output support
Automatic format detection
Better audio quality tuning
Full protocol documentation

Related Project

This project is part of a larger reverse engineering effort:

 EZW-RT10A emulation using STM32
 Sony transmitter emulation (ESP32)

 Warranty & Responsibility

This software and all associated hardware designs are provided “as is”, without any warranty of any kind.

By using this project, you acknowledge and agree that:

There is no warranty, express or implied, including but not limited to:
fitness for a particular purpose
reliability or accuracy
compatibility with your hardware
You are using this software and hardware entirely at your own risk
The author shall not be held responsible for any:
hardware damage
data loss
system malfunction
financial loss
or any other direct or indirect damages

User Responsibility

You are fully responsible for:

verifying all connections before powering hardware
ensuring proper voltage levels and grounding
testing on non-critical devices first
complying with applicable laws and regulations
Important Notice

This project involves reverse engineering and low-level hardware interaction.
Incorrect usage may result in permanent damage to devices.

Final Statement

Use this software and information at your own responsibility.



