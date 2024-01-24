# Miniflie
Miniflie is an experimental minimalist version of the 
[Crazyflie firmware](https://github.com/bitcraze/crazyflie-firmware),
with support for SITL simulation in Webots.  This minimalist version doesn't 
support Loco positioning or swarms, so for running your Crazyflie
you'll probably want to use the official Crazyflie firmware.

If you do want to try out Miniflie, here's how:

## Webots

1. Install [Webots](https://cyberbotics.com/) on your computer.  

2. Launch Webots and use <b>File / Open World</b> to open <b>Minifile/webots/worlds/simple.wbt</b>.

3.  Build and run the code.  If you have a game controller or R/C/ transmitter with
adapter dongle, you can use that to fly;  otherwise, the simulator will tell you 
how to fly using the keybord.

## Crazyflie (2.1 and Bolt 1.1)

1. Install [STM32Duino](https://github.com/stm32duino)

2. Edit <tt>Miniflie/crazyflie/Makefile</tt> to reflect where you 
installed STM32Duino

3. <tt>cd Miniflie/crazyflie</tt>

4. For CF2, <tt>make clean && make cf2_defconfig</tt> 

5. For Bolt, <tt>make clean && make bolt_defconfig</tt> 

6. <tt>make cload</tt>


