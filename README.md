# DrawBoy - CompuDobby III loom driver

# SYNOPSIS

**DrawBoy** \[*Options*\] \<*wif file path*\>

# DESCRIPTION

**DrawBoy** is a tool for driving an AVL CompuDobby III loom from a
terminal interface. The picks in the specified WIF file are sent to the
loom over a serial interface as the user treadles the draft. In addition
to responding to treadle events from the loom, DrawBoy is interactive
and responds to a number of commands: reversing weaving direction, tabby
mode vs. lift-plan mode, changing the current pick, and changing the set
of picks to be woven. The up and down arrow keys can also be used to
quickly change to adjacent picks.

The terminal should support xterm/ANSI style control sequences and
Unicode, which pretty much all do these days. If the terminal does not
display the Unicode characters used by DrawBoy then the **\--ascii**
option will force DrawBoy to restrict itself to ASCII characters. If
ANSI styling (bold, color, etc.) do not display properly then the
**\--ansi=no** option will force plain-text output. Some terminals
support 24-bit color, which DrawBoy can take advantage of (macOS
Terminal does not). Use the **\--ansi=truecolor** option to enable
24-bit TrueColor output.

# OPTIONS

**\-h**, **\--help**

> Outputs a help message and exits.

**\--findloom**

> Engages the loom finder wizard to help the user determine which device file is the USB serial port interface. Fixed (non-USB) serial ports cannot be found using the loom finder wizard, consult your system documentation.

**\-p**_pick_, **\--pick**=*pick*

> Sets the initial pick from the pick list to start weaving. The default pick is the first pick in the pick list. If the specified pick is larger than size of the pick list then a modulo calculation is performed to determine where to start weaving in the pick list.

**\-P**_picklist_, **\--picks**=*picklist*

> Sets the list of picks from the WIF file to weave. This is a comma-separated list of pick ranges with an optional multiplier in front. The optional multiplier is a number followed by the letter *x*. The pick range can a single number, a pair of numbers separated by a hyphen, or an arbitrary list of tabby picks (ab).

> Pick ranges can also insert a tabby pick with each pattern pick from the WIF file. A single pick preceeded by a tilde (~) will insert the appropriate tabby pick along with the specified WIF pick. A range of picks separated by a tilde instead of a hyphen will insert a tabby pick with each WIF pick.

> The default pick list is the entire WIF file.

**\--tabby**=*tabby spec*

> Sets the tabby for the current weaving. The *tabby spec* is a string of the letters A and B. It indicates which shafts are in tabby A and which are in tabby B. For example, **\--tabby=AABBBBBB** indicates that shafts 1 and 2 are tabby A and shafts 3 through 8 are tabby B. The default is odd shafts on tabby A and even shafts on tabby B.

**\--tabbycolor**=*tabby color*

> Specifies what color to display for tabby picks. The *tabby color* is a three or six digit hexadecimal number that indicates an RGB color triple, RRGGBB for six digits and RGB for three digits. The default tabby color is 00FF00, or green.

**\--tabbyPattern**=*tabby pattern*

> Specifies whether inserted tabby picks are before or after the pattern pick and whether to start with tabby A or tabby B. Permitted pattern values are xAyB, AxBy, xByA, and BxAy.

**\--loomDevice**=*loom path*

> The path to the serial port in the /dev directory. The loom path is required for DrawBoy to operate. It can also be specified with the **DRAWBOY_LOOMDEVICE** environment variable.

**\--shafts**=*shaft count*

> Number of shafts supported by the loom. The shaft count is required for DrawBoy to operate. It can also be specified with the **DRAWBOY_SHAFTS** environment variable. Allowed values are 8, 12, 16, 24, 32, and 40.

**\--dobbyType**=*dobby type*

> Whether the loom is a positive dobby (+ or positive), or a negative dobby (- or negative). The dobby type is required for DrawBoy to operate. It can also be specified with the **DRAWBOY_DOBBY** environment variable. Most AVL looms are positive dobbies, but the Workshop Dobby Loom (WDL) is a negative dobby.

**\--ascii**

> Indicates that the terminal does not support Unicode characters and that only ASCII characters can be used. It can also be specified with the **DRAWBOY_ASCII** environment variable.

**\--ansi**=*ANSI support*

> Indicates whether the terminal supports ANSI display modes (e.g., bold or color). Accepted values are **yes** for normal ANSI support, **no** for no ANSI support, and **truecolor** for ANSI support with 24-bit color. It can also be specified with the **DRAWBOY_ANSI** environment variable.

# OPERATION

When DrawBoy is started it does not know the state of the loom, whether
the shed is open or closed, whether or not it is OK to send shaft
commands to the loom. It will display the first pick in your pick list
with the **PENDING** flag at the end of the pick. Treadle to open and
close the shed. This will sync DrawBoy with the loom, it will send the
liftplan for the first pick to the loom and you can now start weaving.

From this point on, when you close the shed DrawBoy will send the
liftplan for the next pick to the loom and it will display the next
pick. The pick line will begin with a color-coded draw-down for the
pick, followed by the pick number and lift-plan for the pick. These last
two will be color-coded with the weft color for the pick.

Under the pick line will be the input prompt for the current pick. It
will display the current weaving mode (lift-plan or tabby) and the
current line in the WIF file. Treadling the loom will advance to the
next pick. At any time, the user can type commands to change the
behavior of DrawBoy:

l - **Lift-plan Mode**

> DrawBoy weaves picks from the pick list.

t - **Tabby Mode**

> DrawBoy weaves alternating picks of tabby A and tabby B.

r - **Reverse Weaving**

> Changes the weaving direction from the pick list. This has no effect in tabby mode. But in lift-plan mode it determines whether to move forward or backward through the pick list.

s - **Select Pick**

> Select the next pick in the pick list. The up and down arrows act as shortcuts for selecting the previous or next pick in the pick list.

p - **Change Pick List**

> Overrides the pick list. The pick list specification is the same format as with the command line option. Changing the pick list resets the pick to 1.

q - **Quit**

> Quits DrawBoy.

# EXAMPLES

## Setting the pick list:

Here is an example of the various elements in a pick list.
```
DrawBoy --picks=1-8,7-1,10,5x20-29,7x40 towels.wif
```
This sets the pick list:  
1 2 3 4 5 6 7 8  
7 6 5 4 3 2 1  
10  
20 21 22 23 24 25 26 27 28 29  
20 21 22 23 24 25 26 27 28 29  
20 21 22 23 24 25 26 27 28 29  
20 21 22 23 24 25 26 27 28 29  
20 21 22 23 24 25 26 27 28 29  
40 40 40 40 40 40 40


Here a compact draft for a braided twill has various subparts
multiplied to produce 2.5\" headers and an overall length of 27\",
with waste yarn picks to indicate the cut line between towels.
```
DrawBoy --picks=7x1-8,9-60,4x61-212,7x213-220,AB "braided twill towel.wif"
```
The braided twill towel starting with 56 picks (7x1-8) of basket weave
header. Then we have 52 picks (9-60) pattern lead-in. Then 608 picks
of the main body of the towel (4x61-212). 56 more picks of basket
weave for the footer. Lastly, two picks of tabby with waste yarn to
show the cut line between towels.

## Continuing between weaving sessions:

DrawBoy does not remember where you are weaving between sessions.
Instead, the history buffer of the terminal is used to remember the
weaving state.
```
% DrawBoy "fancy twill.wif"
||---||--||--|--||--||---||--||--|--||--||---||--||--|    1 -->  | ** ** *|
-||-||--||--|||--||--||-||--||--|||--||--||-||--||--||    2 -->  | * *  * |
--|||--||--||-||--||--|||--||--||-||--||--|||--||--||-    3 -->  |*  *  * |
|--|--||--||---||--||--|--||--||---||--||--|--||--||--    4 -->  |* * ** *|
||---||--||--|--||--||---||--||--|--||--||---||--||--|    5 -->  | ** ** *|
-|--||--||--||---||--|--||--||--||---||--|--||--||--||    6 -->  | * * ** |
---||--||--||--|--||---||--||--||--|--||---||--||--||-    7 -->  |*  * ** |
--||--||--||--||---|--||--||--||--||---|--||--||--||--    8 -->  |* * * **|
-||--||--||--||--||--||--||--||--||--||--||--||--||--|    9 -->  | ** * * |
||--||---|--||--||--||--||---|--||--||--||--||---|--||   10 -->  | *** * *|
|--||--|---||--||--||--||--|---||--||--||--||--|---||-   11 -->  |* ** * *|
```
time passes
```
|--|--||--||---||--||--|--||--||---||--||--|--||--||--  480 -->  |* * ** *|
||---||--||--|--||--||---||--||--|--||--||---||--||--|  481 -->  | ** ** *|
-|--||--||--||---||--|--||--||--||---||--|--||--||--||  482 -->  | * * ** |
---||--||--||--|--||---||--||--||--|--||---||--||--||-  483 -->  |*  * ** |
--||--||--||--||---|--||--||--||--||---|--||--||--||--  484 -->  |* * * **|
-||--||--||--||--||--||--||--||--||--||--||--||--||--|  485 -->  | ** * * |
||--||---|--||--||--||--||---|--||--||--||--||---|--||  486 -->  | *** * *|
|--||--|---||--||--||--||--|---||--||--||--||--|---||-  487 -->  |* ** * *|
--||---||--|--||--||--||---||--|--||--||--||---||--|--  488 -->  |*  ** * |
-||-||--||--|||--||--||-||--||--|||--||--||-||--||--||  489 -->  | * *  * |
||---||--||--|--||--||---||--||--|--||--||---||--||--|  490 -->  | ** ** *|
|--|--||--||---||--||--|--||--||---||--||--|--||--||--  491 -->  |* * ** *|
--|||--||--||-||--||--|||--||--||-||--||--|||--||--||-  492 -->  |*  *  * |
-||-||--||--|||--||--||-||--||--|||--||--||-||--||--||  493 -->  | * *  * |
[Weaving:45]  T)abby  L)iftplan  R)everse  S)elect pick  P)ick list  Q)uit
```
It\'s time to turn the loom off for the night, so you quit DrawBoy. The
next day you continue weaving. The draw-down from the previous days
weaving is still visible in your terminal window and the last pick was
493.
```
% DrawBoy --pick=493 "fancy twill.wif"
-||-||--||--|||--||--||-||--||--|||--||--||-||--||--||  493 -->  | * *  * |
||---||--||--|--||--||---||--||--|--||--||---||--||--|  494 -->  | ** ** *|
|--|--||--||---||--||--|--||--||---||--||--|--||--||--  495 -->  |* * ** *|
--||---||--|--||--||--||---||--|--||--||--||---||--|--  496 -->  |*  ** * |
-||--|--||---||--||--||--|--||---||--||--||--|--||---|  497 -->  | * ** * |
||--||---|--||--||--||--||---|--||--||--||--||---|--||  498 -->  | *** * *|
|--||--|---||--||--||--||--|---||--||--||--||--|---||-  499 -->  |* ** * *|
--||--||--||--||---|--||--||--||--||---|--||--||--||--  500 -->  |* * * **|
-||--||--||--||--|---||--||--||--||--|---||--||--||--|  501 -->  | ** * **|
-|--||--||--||---||--|--||--||--||---||--|--||--||--||  502 -->  | * * ** |
--|||--||--||-||--||--|||--||--||-||--||--|||--||--||-  503 -->  |*  *  * |
[Weaving:55]  T)abby  L)iftplan  R)everse  S)elect pick  P)ick list  Q)uit
```

# ENVIRONMENT

The following environment variables affect the behavior of DrawBoy. They
provide information that will likely be common to all DrawBoy runs. It may
be useful to set them in the users account profile.

**DRAWBOY_LOOMDEVICE**

Indicates the path to the serial device for talking to the loom.

**DRAWBOY_SHAFTS**

Indicates how many shafts the loom supports.

**DRAWBOY_DOBBY**

Indicates whether the loom has a positive dobby (positive or +)or a negative dobby (negative or -).

**DRAWBOY_ASCII**

If it exists then DrawBoy will only output ASCII characters.

**DRAWBOY_ANSI**

Indicates the ANSI support level for the terminal.
