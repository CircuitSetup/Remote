## Hardware

This document contains information on buttons/switches as well as instructions on how to modify your Futaba remote control to work with the kit Control Board.

### Power supply

The prop is powered by a 5V battery, connected to the USB port of the ESP32 board on top of the Control Board. It should be able to deliver 1A, and have a capacity of >= 2500mAh.

Depending on the shape of the battery, it can be mounted under the Unibox (instead of the second set of 9V batteries), or inside the Futaba, in place of the Futaba's original battery. In the latter case, access to the battery for charging has to be provided, since the Futaba's battery compartment is not accessible without major disassembly. One way to do this is to use two pins of the existing charging connector.

If you don't want to pull out the USB cable from the battery to power down, you can use the "UP/DOWN" switch on the Unibox; all you need are two open-end USB power cables (one of type "micro USB" for the ESP32, one depending on the output plug type of your battery), and a maintained switch as "UP/DOWN". 

### Button/switch mapping

The firmware has the following minimum requirements:
- Fake Power switch (maintained)
- Buttons "O.O" and "RESET" (various functions; momentary)
- Calibration button (momentary)

Additionally, up to eight "User Buttons" can be installed, used for audio playback and/or MQTT messaging. These buttons can be momentary or maintained contacts.

Fake power (ON/OFF), "O.O" and "RESET" are on the Unibox. This leaves Calibration and, optionally, User Buttons to assign.

"UP/DOWN" and "HOLD" on the Unibox are free to use; however, one of those should be used as a real power switch, separating the battery from the Control Board (unless you want to power down your Futaba prop by pulling out the USB cable from the battery). The other can, for instance, be equipped with a 3-position MOM-OFF-MOM or ON-OFF-MOM flat handle mini toggle switch, serving as "Calibration" (MOM) and one User Button. 

If you are willing to modify your Futaba beyond connecting the throttle pot, you can also wire its "Fail Safe Set" button as "Calibration", and/or use the Futaba's toggle switches on the top and front as User Buttons.

Example schematics:

![schematics](img/schematics.png)

The switch for User Button 4 and 5 in above example must be a three-position switch with OFF in the middle.

### How to take apart the Futaba remote control

The following instructions only apply to models FP-T8SGA-P and FP-T8SGH-P.

![part1](img/htof1.jpg)

1) Remove the cover of the adjustment panel (1) on the back
2) Remove the transmitter module (2); it is held by two clips at top and bottom
3) Remove the four silver screws (3) on the back and remove back cover

![part2](img/htof2.jpg)

4) Remove four black screws (4) on the adjustment panel, and remove the bezel

![part3](img/htof3.jpg)

5) Remove two black screws (5) on the front, in the outer corners of the lever controls, and note which screw goes into which hole (they are different)

![part4](img/htof4.jpg)

6) Remove four black screws (6) on the bottom

![part5](img/htof5.jpg)

7) Pull out side parts; the small switch panels can be pulled out of the side parts easily.

![part6](img/htof6.jpg)

9) Remove four silver screws (7) around the antenna, and pull out the antenna as far as the wire permits; you could as well cut off that wire since the antenna isn't used.

![part7](img/htof7.jpg)

10) Remove two silver screws (8) on the top of the main PCB
11) Unplug five ribbon cables (9) on the main PCB
12) Lift main PCB; at this point, it is held by only pin sockets that connect it to other PCBs; you can't pull it out entirely because it is connected to other parts by wires. Just pull it out and downwards, that way you can access all relevant parts.

Now you can access the potentiometer to patch in black-green-red wires in order to connect this pot to the kit's Control Board, as well as all switches and buttons you may want to connect to said Control board.

It is recommended to remove the Futaba's battery. It is heavy, removing it makes the finished prop easier to handle. Also, since the the Remote, after the modifications, can't be used normally anyway, there is no point in keeping a battery in it. The space can be used for a rechargeable battery to power the kit.

### Throttle connection

![pot](img/pot.jpg)

Desolder the red/green/black wires from the pot, located top left hand side (looking at it from the back), and attach new ones to connect the pot to the Control Board. The connector on the Control Board is marked R, G, Bk, matching the original wire colors.

Please note that the kit electronics are designed for models FP-T8SGA-P and FP-T8SGH-P, which use a 5k pot and resistancies of 1.8k through 3k for the throttle. If you are using a different model, the electronics might need modifications. Please contact us for more information.

### Button/switch connections

#### Toggle switches

Note that using the Futaba's original front and top toggle switches is entirely optional, and doing it is not trivial. They would serve as User Buttons for audio playback and MQTT messages.

If you want to wire the toggle switches from the Futaba remote control to the Control Board, it is recommended to use a common GND wire. This GND wire connects all center pins of all switches to each other and to the GND screw connector on the Control Board.

Some of the toggle switches on the Futaba are interconnected through their PCBs, such as the ones on top, left hand side (when viewed from the back). Check the traces of the PCBs, desoldering or cutting-off components or wire bridges might be required. The switches must be working independingly, otherwise proper operation might be hindered.

Important: Do not re-connect the two widest ribbon cables (top two ones) to the main PCB when re-assembling the remote; other parts of the original circuit might interfere with toggle switch operation.

#### Calibration

The "Fail Safe Set" button on the back makes an excellent "Calibration" button. Desolder the green and yellow wires, and connect your new wires to the Control Board in their place:

![FailSafeSet](img/fss.jpg)

### Power LED

Connecting the Control Board to the Futaba's power LED is optional, and this LED is a bit tricky to access, Its solder points are on the center PCB, below the power meter, under a "flying" pot that looks like a white square. You can bend up that pot to reach the solder points. Left is GND, right is VCC (when viewed from the back). You need to cut the traces on that PCB that lead away from the LED's solder points because the remaining circuit might interfere with LED operation.

Don't forget to bend the "flying pot" back after soldering, otherwise you can't put in the antenna.

![PowerLED](img/pled.jpg)

### Battery Level Meter

Connecting the Control Board to the Futaba's level meter is, of course, optional. As far as I could make out, it is never showing anything but "zero" in the movie. In order to connect it to the Control Board, solder two wires to the meter's contact like shown below.

![PowerLED](img/blvl1.jpg)

![PowerLED](img/blvl2.jpg)

### Wires

The wires need to lead out of the Futaba's body. One way to do this is to dremel off the back cover a bit to make room, as shown in the picture. This requires bending the top part of the inner shielding upward (or breaking it off) to make room for the wires in the corners.

![wires](img/wires2.jpg)

![wires](img/wires.jpg)

Final step is to take apart the transmission module, and only insert the lid into the slot.

![wires](img/wires3.jpg)

### "Stop" light and switch

Most people use tape to attach the light and the switch. Since the Futaba's surface isn't really suited for tape at the proper places, and the area's for tape are quite small, I went for screws instead. I used M2 screws to attach both the light and the switch. 

![stop1](img/stoplight1.jpg)

![stop2](img/stoplight2.jpg)

![stops1](img/stopswitch1.jpg)

![stops2](img/stopswitch2.jpg)

### Box attachment

Since the transmitter module's lid isn't entirely fixed (if you followed my route above), the box is far more better attached using 6mm PCB standoffs:

![standoffs](img/boxstandoffs.jpg)

### Box

![box1](img/box1.jpg)

![box2](img/box2.jpg)

_Text & images: (C) Thomas Winischhofer ("A10001986"). See LICENSE._ Source: https://remote.out-a-ti.me

