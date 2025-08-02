# motor-man
This uses a cheap ESP32 display (CYD - Cheap Yellow Display)  with Max3232 serial chip (also cheap)  to control and display your Curtis Motor controller vitals.

## Get a built motor-man as a gift.. from the universe.. 
If you DON'T want the science experiment, and JUST want a touch screen display with serial chip and 10 feet of cable to connect to your existing Curtis controller and get all the cool features (including the digital rain lock screen) .. then hit up the [Little Bleu Art Car](https://littlebleu.org) site. Your Gift makes it possible to continue the artistic endeavor and inspire builders everywhere to make something beuatiful no matter what the medium. At checkout put "DIGITALRAIN" or contact gifts@littlebleu.org and we'll contact you to get a kit shipped from the universe to you. 

## Project Overview
(yes.. cheaper than Alltrax) - I think alltrax is a premium well built product, but unfortunately everything we do is on a budget. We can buy several 500 Amp Curtis controllers for the price of one 500 Amp Alltrax.. (if some-one wants to donate .. a gently used Alltrax controller.. that would facilitate updating this project to control alltrax controllers..) and I wouldn't mind putting one in the Art Car for Road Testing.. 

If you want a delightful journay.. Read on! 

This project allows you to
- Monitor various power and throttle metrics
- calculated speedomoter. (you'll need to calibrate as it varies based on your tire size, axle ratio, and other variables)
- control direction (forward and back)
- lock the motor to a given speed - kid mode. 
- lock out control changes - for adult time.
- impress your friends with some digital rain in your cart.

In the future: you could potentially:
- Control an Alltrax controller (shameless plug here for a unit to test this project.) 
- Tune several other parameters in the controller. Acceleration speeds, deadband range, regenerative braking parameters, current limits, etc.
- drive by wire.. I'm gonna .. test this well before I loose that dragon.

Additional Features
- ESP-NOW Friendly: If you have a 10 foot long 1968 Taylor-Dunn B2-48 like [little Bleu](https://littlebleu.org) then you'll appreciate the ESP-NOW features allowing you to have one Display Controller on the Motor and one Display Controller up on the driver dash without any cables in between for all your controls and vital displays. The only wires you'll still need are the speed control.  I haven't myself decided to bite off on the drive by wire over the ESP-NOW network... yet..

## Project Setup

###Software:
1. This uses the Arduino IDE.

###Hardware - These have Amazon links.. when you use these, we put just use the ripple in fundage that to make more projects and tutorials here at the farm. 

###Hardware:
1. A [Cheap Yellow Display]() (CYD). Many available on the market. Its actually called an "ESP32-2432SO28R" or oddly "ES32-2432SO28" so you can see why .. it gets shortened to CYD.  You can get 2 for half the price of a steak at Ruth Chris on that Jungle store [->here<-](https://amzn.to/3IRPpGp) - The ones I received came without the "R" which is supposed to mean it has the resistive display.. however this unit does have the touch chip and the touch displays do work. 

    1a. biggest issue is that the default SPI_FREQUENCY in the user setup. For whatever reason it defaults to 55MHz. change this to 27MHZ which is the max speed of the touch controller
```
...
// #define SPI_FREQUENCY  20000000
#define SPI_FREQUENCY  27000000
// #define SPI_FREQUENCY  40000000
// #define SPI_FREQUENCY  55000000 // STM32 SPI1 only (SPI2 maximum is 27MHz)
```

    1b. Arduino Module set it to ESP-WROOM32-DA
    1c. the port defaults to weird baud rates by default and can't upload due to that. Change it in tools -> upload speed -> 460800

    1d. NOW the test program from random nerd tutorial `cyd-touch-test.ino` to test the touch screen should work. 
2. A not too cheap max 3232 chip to adjust the 3.3 volt serial communication levels to TTL (and sometimes reverse TTL signal levels.)
3. A [curtis 1205-117 motor controller](https://amzn.to/3ILqwMN). You can get the lower capacity cheaper model, or just get this half the price - twice the capacity and limit the current to your carts capabilities. I don't know why but there are now many knock offs on the market. An original from Curtis will run you around the cost of a Tank of Gas for your Boat.. Highly recommend for any serious applications.  A knock off like this [Vevor 36V DC Motor Controller 1205-117](https://amzn.to/3ILqwMN) you can score on Amazon for around the cost of a Movie Ticket and includes a 2 year warranty. I decided to go this route as I needed one on the bench for R&D and the other in the cart for "Road Testing" - And if my project went haywaire, I was only destroying a knock off and there were no good movies out this summer anyway..
4. A Single Pull Double Throw 36/48Volt Relay (SPDT) if you want to use the forward reverse feature. 

###Additional resources: 
- Buggies gone wild has many of the control codes from the curtis controllers. The ones I have used and the control mechanism itself in this project were garnered from the thread in the user groups.
- Scott at [CartsUnlimited.net](CartsUnlimited.net) -> highest recommends here.. This project is not for converting your cart.. If you are doing a conversion and are not electrically inclined, consider getting one of the carts unlimited COMPLETE quality kits.. These kits have everything you need to do a conversion with wiring diagrams and quality components. You will save time and money (and friends and hair) straight up buying one of these conversion kits. Plus you get Scott supporting your conversion.
- [Random Nerd Tutorials](https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/) is a great place to get some familiarity - test programs.. an sanity with the CYD Boards. 

## Software Setup
1. Load the [Arduio IDE](https://www.arduino.cc/en/software/)  -- its free or grab the Cloud Arduino (its not free but nice if you use it a-lot across multiple computers.)
2. Load the libraries needed to work with the Display - Several good tutorial from [Random Nerd Tutorials](https://randomnerdtutorials.com/ on how to work with these displays including a proper download User_Setup.h which you'll need to update to get the CYD working
- IF DISPLAY WORKS BUT TOUCH IS NOT WORKING -- if you are using the [Amazon CYD displays](https://amzn.to/3IRPpGp) with the [Random Nerd Tutorials](https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/), Don't forget to slow down the SPI_FREQUENCY to 27MHZ so that touch will work. 
3. 
