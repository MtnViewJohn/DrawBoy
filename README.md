# drawboy - CompuDobby III loom driver

# SYNOPSIS

**drawboy** \[*Options*\] \<*draft file path*\>

# DESCRIPTION

**drawboy** is a tool for driving an AVL CompuDobby III loom from a
terminal interface. The picks in the specified draft file are sent to the
loom over a serial interface as the user treadles the draft. In addition
to responding to treadle events from the loom, **drawboy** is interactive
and responds to a number of commands: reversing weaving direction, tabby
mode vs. lift-plan mode, changing the current pick, and changing the set
of picks to be woven. The up and down arrow keys can also be used to
quickly change to adjacent picks.

The draft file can either be in the standard WIF format or in the Fiberworks
DTX format.

The terminal should support xterm/ANSI style control sequences and
Unicode, which pretty much all do these days. If the terminal does not
display the Unicode characters used by **drawboy** then the **&#8209;&#8209;ascii**
option will force **drawboy** to restrict itself to ASCII characters. If
ANSI styling (bold, color, etc.) does not display properly then the
**&#8209;&#8209;ansi=no** option will force plain-text output. Some terminals
support 24-bit color, which **drawboy** can take advantage of (macOS
Terminal does not). Use the **&#8209;&#8209;ansi=truecolor** option to enable
24-bit TrueColor output.

# OPTIONS

**\-h**, **\--help**

> Outputs a help message and exits.

**\-\-findloom**

> Engages the loom finder wizard to help the user determine which device file is the USB serial port interface. Fixed (non-USB) serial ports cannot be found using the loom finder wizard, consult your system documentation. The wizard can output the possible device paths for fixed serial ports.

**\-p**_pick_, **\-\-pick**=*pick*

> Sets the initial pick from the pick list to start weaving. The default pick is the first pick in the pick list. If the specified pick is larger than size of the pick list then a modulo calculation is performed to determine where to start weaving in the pick list.

**\-P**_picklist_, **\-\-picks**=*picklist*

> Sets the list of picks from the draft file to weave. This is a comma-separated list of pick ranges with an optional multiplier in front. The optional multiplier is a number followed by the letter *x*. The pick range can a single number, a pair of numbers separated by a hyphen, or an arbitrary list of tabby picks (ab).

> Pick ranges can also insert a tabby pick with each pattern pick from the draft file. A single pick preceeded by a tilde (~) will insert the appropriate tabby pick along with the specified draft pick. A range of picks separated by a tilde instead of a hyphen will insert a tabby pick with each draft pick.

> The default pick list is the entire draft file.

**\-\-tabby**=*tabby spec*

> Sets the tabby for the current weaving. The *tabby spec* is a string of the letters A and B. It indicates which shafts are in tabby A and which are in tabby B. For example, **\--tabby=AABBBBBB** indicates that shafts 1 and 2 are tabby A and shafts 3 through 8 are tabby B. The default is odd shafts on tabby A and even shafts on tabby B.

**\-\-tabbycolor**=*tabby color*

> Specifies what color to display for tabby picks. The *tabby color* is a three or six digit hexadecimal number that indicates an RGB color triple, RRGGBB for six digits and RGB for three digits. The default tabby color is 00FF00, or green.

**\-\-tabbyPattern**=*tabby pattern*

> Specifies whether inserted tabby picks are before or after the pattern pick and whether to start with tabby A or tabby B. Permitted pattern values are xAyB, AxBy, xByA, and BxAy. Default is xAyB.

**\-\-loomDevice**=*loom path*

> The path to the serial port in the /dev directory. The loom path is required for **drawboy** to operate. It can also be specified with the **DRAWBOY_LOOMDEVICE** environment variable.

**\-\-shafts**=*shaft count*

> Number of shafts supported by the loom. The shaft count is required for **drawboy** to operate. It can also be specified with the **DRAWBOY_SHAFTS** environment variable. Allowed values are 8, 12, 16, 24, 32, and 40.

**\-\-dobbyType**=*dobby type*

> Whether the loom is a positive dobby (+ or positive), or a negative dobby (\- or negative). The dobby type is required for **drawboy** to operate. It can also be specified with the **DRAWBOY_DOBBY** environment variable. Most AVL looms are positive dobbies, but the Workshop Dobby Loom (WDL) is a negative dobby.

**\-\-ascii**

> Indicates that the terminal does not support Unicode characters and that only ASCII characters can be used. It can also be specified with the **DRAWBOY_ASCII** environment variable.

**\-\-ansi**=*ANSI support*

> Indicates whether the terminal supports ANSI display modes (e.g., bold or color). Accepted values are **yes** for normal ANSI support, **no** for no ANSI support, and **truecolor** for ANSI support with 24-bit color. It can also be specified with the **DRAWBOY_ANSI** environment variable.

# OPERATION

When **drawboy** is started it does not know the state of the loom, whether
the shed is open or closed, whether or not it is OK to send shaft
commands to the loom. It will display the first pick in your pick list
with the **PENDING** flag at the end of the pick. Treadle to open and
close the shed. This will sync **drawboy** with the loom, it will send the
liftplan for the first pick to the loom and you can now start weaving.

From this point on, when you close the shed **drawboy** will send the
liftplan for the next pick to the loom and it will display the next
pick. The pick line will begin with a color-coded draw-down for the
pick, followed by the pick number and lift-plan for the pick. These last
two will be color-coded with the weft color for the pick.

Under the pick line will be the input prompt for the current pick. It
will display the current weaving mode (lift-plan or tabby) and the
current line in the draft file. Treadling the loom will advance to the
next pick. At any time, the user can type commands to change the
behavior of **drawboy**:

l - **Lift-plan Mode**

> **drawboy** weaves picks from the pick list.

t - **Tabby Mode**

> **drawboy** weaves alternating picks of tabby A and tabby B.

r - **Reverse Weaving**

> Changes the weaving direction from the pick list. This has no effect in tabby mode. But in lift-plan mode it determines whether to move forward or backward through the pick list.

s - **Select Pick**

> Select the next pick in the pick list. The up and down arrows act as shortcuts for selecting the previous or next pick in the pick list.

p - **Change Pick List**

> Overrides the pick list. The pick list specification is the same format as with the command line option. Changing the pick list resets the pick to 1.

q - **Quit**

> Quits **drawboy**.

# EXAMPLES

## Setting the pick list:

Here is an example of the various elements in a pick list.
```
drawboy --picks=1-8,7-1,10,5x20-29,7x40 towels.wif
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
multiplied to produce 2.25\" headers and an overall length of 32\",
at 24ppi with waste yarn picks to indicate the cut line between towels.
```
drawboy --picks=7x1-8,24x9-36,93-100,7x101-108,AB "gudruns towel4.wif"
```
The braided twill towel starting with 56 picks (7x1-8) of basket weave
header. Then the 28 pick repeating part of the pattern is repeated 24 times to 
get 672 picks of the main body of the towel (24x9-36). Then we have 8 picks 
(93-100) pattern trail-out. 56 more picks of basket weave for the footer. 
Lastly, two picks of tabby with waste yarn to show the cut line between towels.

## Continuing between weaving sessions:

**drawboy** does not remember where you are weaving between sessions.
Instead, the history buffer of the terminal is used to remember the
weaving state.
```
% drawboy --picks=7x1-8,24x9-36,93-100,7x101-108,AB "gudruns towel4.wif"
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--    1 -->  | ** * * |
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--    2 -->  | ** * * |
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||    3 -->  |*  * * *|
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||    4 -->  |*  * * *|
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--    5 -->  | ** * * |
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--    6 -->  | ** * * |
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||    7 -->  |*  * * *|
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||    8 -->  |*  * * *|
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--    9 -->  | ** * * |
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--   10 -->  | ** * * |
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||   11 -->  |*  * * *|
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||   12 -->  |*  * * *|
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--   13 -->  | ** * * |
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--   14 -->  | ** * * |
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||   15 -->  |*  * * *|
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||   16 -->  |*  * * *|
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--   17 -->  | ** * * |
```
time passes
```
||-||--|||--||--||--||-||--|||--||--||--||-|||--||--||--||  303 -->  |* *  * *|
|---||--|--||--||--||---||--|--||--||--||---||--||--||--||  304 -->  |*  ** * |
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--  305 -->  | ** * * |
-||--||--||--||--||--||--||--||--||--||--||---||--||--||--  306 -->  | * * * *|
||--||--||--|--||---||--||--||--|--||---||--||--||--||--||  307 -->  |*  * ** |
|--||--||--|||--||-||--||--||--|||--||-||--|||--||--||--||  308 -->  |* * *  *|
--||--||--|--||--||---||--||--|--||--||---||--||--||--||--  309 -->  | ** ** *|
-||--||--|||--||--||-||--||--|||--||--||-||---||--||--||--  310 -->  | * *  * |
||--||--||-||--||--|||--||--||-||--||--|||--||--||--||--||  311 -->  |*  *  * |
|--||--||---||--||--|--||--||---||--||--|--|||--||--||--||  312 -->  |* * ** *|
--||--||--|--||--||---||--||--|--||--||---||--||--||--||--  313 -->  | ** ** *|
-||--||--|||--||--||-||--||--|||--||--||-||---||--||--||--  314 -->  | * *  * |
||--||--||-||--||--|||--||--||-||--||--|||--||--||--||--||  315 -->  |*  *  * |
|--||--||---||--||--|--||--||---||--||--|--|||--||--||--||  316 -->  |* * ** *|
--||--||--||-||--|||--||--||--||-||--|||--||--||--||--||--  317 -->  | ** *  *|
-||--||--||---||--|--||--||--||---||--|--||---||--||--||--  318 -->  | * * ** |
||--||--||--||--||--||--||--||--||--||--||--||--||--||--||  319 -->  |*  * * *|
|--||--||--||--||--||--||--||--||--||--||--|||--||--||--||  320 -->  |* * * * |
--|--||---||--||--||--|--||---||--||--||--|---||--||--||--  321 -->  | * ** * |
-|||--||-||--||--||--|||--||-||--||--||--|||--||--||--||--  322 -->  | **  * *|
[Weaving:22] T)abby  L)iftplan  R)everse  S)elect pick  P)ick list  Q)uit
%
```
It is time to turn the loom off for the night, so you quit **drawboy**. The
next day you continue weaving. The draw-down from the previous days
weaving is still visible in your terminal window and the last pick was
322.
```
% drawboy --picks=7x1-8,24x9-36,93-100,7x101-108,AB "gudruns towel4.wif --pick=322
-|||--||-||--||--||--|||--||-||--||--||--|||--||--||--||--  322 -->  | **  * *|
|--||--||---||--||--|--||--||---||--||--|--|||--||--||--||  323 -->  |* * ** *|
||--||--||-||--||--|||--||--||-||--||--|||--||--||--||--||  324 -->  |*  *  * |
-||--||--|||--||--||-||--||--|||--||--||-||---||--||--||--  325 -->  | * *  * |
--||--||--|--||--||---||--||--|--||--||---||--||--||--||--  326 -->  | ** ** *|
|--||--||---||--||--|--||--||---||--||--|--|||--||--||--||  327 -->  |* * ** *|
||--||--||-||--||--|||--||--||-||--||--|||--||--||--||--||  328 -->  |*  *  * |
-||--||--|||--||--||-||--||--|||--||--||-||---||--||--||--  329 -->  | * *  * |
--||--||--|--||--||---||--||--|--||--||---||--||--||--||--  330 -->  | ** ** *|
||-||--|||--||--||--||-||--|||--||--||--||-|||--||--||--||  331 -->  |* *  * *|
|---||--|--||--||--||---||--|--||--||--||---||--||--||--||  332 -->  |*  ** * |
--||--||--||--||--||--||--||--||--||--||--||--||--||--||--  333 -->  | ** * * |
-||--||--||--||--||--||--||--||--||--||--||---||--||--||--  334 -->  | * * * *|
||--||--||--|--||---||--||--||--|--||---||--||--||--||--||  335 -->  |*  * ** |
|--||--||--|||--||-||--||--||--|||--||-||--|||--||--||--||  336 -->  |* * *  *|
--||--||--|--||--||---||--||--|--||--||---||--||--||--||--  337 -->  | ** ** *|
[Weaving:9] T)abby  L)iftplan  R)everse  S)elect pick  P)ick list  Q)uit   
```

# ENVIRONMENT

The following environment variables affect the behavior of **drawboy**. They
provide information that will likely be common to all **drawboy** runs. It may
be useful to set them in the users account profile.

**DRAWBOY_LOOMDEVICE**

Indicates the path to the serial device for talking to the loom.

**DRAWBOY_SHAFTS**

Indicates how many shafts the loom supports. Accepted values are 8, 12, 16, 24,
32, or 40.

**DRAWBOY_DOBBY**

Indicates whether the loom has a positive dobby (positive or +) or a negative dobby (negative or -).

**DRAWBOY_ASCII**

If it exists then **drawboy** will only output ASCII characters.

**DRAWBOY_ANSI**

Indicates the ANSI support level for the terminal. Accepted values are **yes** 
for normal ANSI support, **no** for no ANSI support, and **truecolor** for ANSI 
support with 24-bit color. 
