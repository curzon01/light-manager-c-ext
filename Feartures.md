# Introduction #

Enhanced version of [light-manager-c](https://code.google.com/p/light-manager-c/), a small linux program which allows you to control your [jbmedia Light Manager](http://cms.jbmedia.de) connected via USB.


# Details #

The program uses LibUSB 1.0, the source given here was compiled and testet on the ARM GNU/Linux box [Raspberry Pi](http://www.raspberrypi.org).

It can be used either
  * as a server program listening
    * either on a TCP port
    * or as a http server
  * as a standalone program executing commands from the command line

The program supports the following features:
  * FS20 commands
  * Uniroll commands
  * InterTechno commands
  * Trigger scenes
  * Reading temperature sensor
  * Reading device clock
  * Set device clock
  * multi TCP client connections
  * Run as a real daemon
  * Optional logging to syslog
  * http get requests


## Compiling ##

  * **Option 1**: Use make with the given Makefile
  * **Option 2**: gcc lightmanager.c -lusb-1.0 -olightmanager

## Usage ##

Start the program using

> `sudo ./lightmanager -?`

to get the full help page:
```
Usage: lightmanager [OPTION]

Options are:
    -c cmd        Execute command <cmd> and exit (separate several commands by ';' or ',')
    -d            Start as daemon (default no)
    -g            Debug mode (default no)
    -h housecode  Use <housecode> for sending FS20 data (default 1111)
    -p port       Listen on TCP <port> for command client (default 3456)
    -s            Redirect output to syslog instead of stdout (default)
    -?            Prints this help and exit
    -v            Prints version and exit
```

When using lightmanager with -c the following cmd are available:
```
Linux Lightmanager (1.2.0014) help

Light Manager commands
    GET CLOCK         Read the current device date and time
    GET HOUSECODE     Read the current FS20 housecode
    GET TEMP          Read the current device temperature sensor
    SET HOUSECODE adr Set the FS20 housecode where
                        adr  FS20 housecode (11111111-44444444)
    SET CLOCK [time]  Set the device clock to system time or to <time>
                      where time format is MMDDhhmm[[CC]YY][.ss]

Device commands
    FS20 addr cmd     Send a FS20 command where
                        adr  FS20 address using the format ggss (1111-4444)
                        cmd  Command ON|OFF|TOGGLE|BRIGHT|+|DARK|-|<dim>
                             ON|UP|OPEN      Switches ON or open a jalousie
                             OFF|DOWN|CLOSE  Switches OFF or close a jalousie
                             +|BRIGHT        regulate dimmer one step up
                             +|DARK          regulate dimmer one step down
                             <dim>           is a absoulte or percentage dim
                                             value:
                                             for absolute dim use 0 (min=off)
                                             to 16 (max)
                                             for percentage dim use 0% (off) to
                                             100% (max)
    IT code addr cmd    Send an InterTechno command where
                        code  InterTechno housecode (A-P)
                        addr  InterTechno channel (1-16)
                        cmd   Command ON|OFF|TOGGLE
    UNIROLL addr cmd  Send an Uniroll command where
                        adr  Uniroll jalousie number (1-100)
                        cmd  Command UP|+|DOWN|-|STOP
    SCENE scn         Activate scene <scn> (1-254)

System commands
    ? or HELP         Prints this help
    EXIT              Disconnect and exit server programm
    QUIT              Disconnect
    WAIT ms           Wait for <ms> milliseconds
```


## Examples ##

### Use it as a server ###

1. First start the server on a given machine where the [jbmedia Light Manager](http://cms.jbmedia.de) is connected to via USB.
Here we want to use housecode 11223344 for FS20 commands and having the server listen on TCP port 55000:
```
sudo ./lightmanager -d -s -h 11223344 -p 55000
```

2. Now you can use
  * a telnet session to communicate with the Light Manager connected on the server (assuming the server can be connected using the name `usb-machine`):
```
telnet usb-machine 55000
Trying 192.168.1.1...
Connected to usb-machine.
Escape character is '^]'.
Welcome to Linux Lightmanager v2.0 (build 0015)
>fs20 1133 on
OK
>fs20 1133 off
OK
>get clock
Thu Aug 16 14:59:24 2012
>get temp
25.5 degree Celsius
>fs20 1133 20%
OK
>quit
bye
Connection closed by foreign host.
```


  * a browser to connect to the Lightmanager http server which then communicates with the Light Manager device connected to the server, for example the url <pre>  http://192.168.1.1:55000/cmd=get temp</pre> returns <pre>  23.5 °C</pre> the url <pre>  http://192.168.1.1:55000/cmd=get time</pre> returns <pre>  Sat Jan 12 15:14:13 2013</pre>Multiple commands can be delimited by a '&' character, example <pre>http://192.168.1.1:55000/cmd=get time&get temp</pre>

### Use it with command line commands ###

#### Set the clock to machine time ####
```
sudo ./lightmanager -c "set clock"
```

#### Read clock and temperature ####
```
sudo ./lightmanager -c "get clock; get temp"
```

#### Dim FS20 dimmer with the address 1133 starting with 25%, ending on 100%, then switch off after 10 seconds ####
```
sudo ./lightmanager -h 11223344 -c "fs20 1133 25%; wait 1000; fs20 1133 50%; wait 1000; fs20 1133 100%; wait 10000; fs20 1133 off"
```