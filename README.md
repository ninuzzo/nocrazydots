# NoCrazyDots
(c) 2017-2018 Antonio Bonifati aka "Farmboy" GPL 3+

NoCrazyDots is many things... a convenient text-mode music notation, a
MIDI player, an electronic band providing auto-accompainment for you,
making easy for a single artist to perform. NoCrazyDots is the only
small program a composer and artist needs to break into the world of
electronic music.

I am a minstrel and poet, check out my free music at
<http://farmboymusicblog.wordpress.com>
You can support me and this free software project by donating new and
used instruments and equipment.


## COMPILE FROM SOURCES

If you do not have Linux, go download and install one even an
abandoned old PC will do (choose a lightweight linux distro for that)

Then run:

```bash
$ make
```

to compile NoCrazyDots.

And:

```bash
$ sudo make install
or
$ su -c 'make install'
```

to install it for use outside of the building directory or multi-user use.

Although not mandatory, it is recommended you run NoCrazyDots as a
user with access to higher ulimits, in order to reduce latency. If you
see a warning like this:

nocrazydots: warning: cannot gain realtime privileges. See README.md

you should take action. In most Linux distro it suffices to add your
user to in a group with the highest rtprio rlimit. You can find it out
whether such a group exists with this command:

```bash
$ grep -ri rtprio /etc/security
/etc/security/limits.d/99-audio.conf:@audio 	- rtprio 	99
/etc/security/limits.conf:#        - rtprio - max realtime priority
```

e.g. in this case you see the name of the group is audio. Else it must
be created and defined similarly, e.g. directly in /etc/security/limits.conf

You can add your user to this group with this command:

```bash
$ sudo gpasswd -a YOUR_USERNAME GROUP
```

Then logout of your user and login again to make this effective, or
just reboot and try to run nocrazydots again.

## USAGE

The nocrazydots executable accepts the following parameters, in no
particular order:

* a single tag character to select the part to
  be played for the auto-accompainment feature.

* a path to the data dir which contains definition of the voice list
  and drumkits. Must end with a / to tell it apart from a score file to play

* a -d or -dump option to dump the raw MIDI protocol bytes, mainly useful
  for debugging purposes

* a percentage of randomization for note velocities

* a + or - followed by the number of semitones to transpose

Score files should be either typed in or loaded using the shell input
redirection (<) or pipes (|) or just named on the command line:

```bash
$ nocrazydots /usr/share/nocrazydots/sample_scores/twinkle.txt
```

If you run nocrazydots without arguments, you can write a one-off score on stdin,
ending with the end-of-file character (ctrl-d). This is useful to try out a
piece of a larger score you copy and paste and/or for testing/learning purposes.

Reading and writing NoCrazyDots scores is simpler than the traditional notation.
Just look at the sample_scores dir and you will understand anything by yourself.

## TROUBLESHOOTING

NoCrazyDots requires a MIDI keyboard equipped with speakers. After
making the USB connection to your computer, wait a few seconds for the
keyboard to initialize. NoCrazyDots should find your keyboard
automatically. If not, you will see the error:

"No MIDI keyboard detected. Try to specify a device name"

and you will have to find out what the device name of the keyboard is.
Simply compare the output of the following command before:

```bash
$ arecord -l
**** List of CAPTURE Hardware Devices ****
card 0: USB [Plantronics .Audio 478 USB], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 2: Generic [HD-Audio Generic], device 0: ALC233 Analog [ALC233 Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 3: Keyboard [Digital Keyboard], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

and after having connected your keyboard:

```bash
$ arecord -l
**** List of CAPTURE Hardware Devices ****
card 0: USB [Plantronics .Audio 478 USB], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 2: Generic [HD-Audio Generic], device 0: ALC233 Analog [ALC233 Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 3: Keyboard [Digital Keyboard], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

E.g. in my case the device name is hw:3,0,0 This is the parameter to
be passed to NoCrazyDots.
