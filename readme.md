# gifw.c

## what's this wizardry?

this little c program takes your favorite gif and plasters it all over your desktop. why? because i can..

<a href="[https://i.imgur.com/r3g38vl.gif](https://i.imgur.com/r3g38vl.gifv)"><img src="[https://i.imgur.com/r3g38vl.gif](https://i.imgur.com/r3g38vl.gifv)" width="60%" align="center"></a>

## features (or "why you need this in your life")

- turns your desktop into a rave party 🕺💃
- impresses your cat (results may vary)
- perfect for hypnotizing your coworkers
- makes you feel like you're living in the future (the future is animated, folks)

## how to make the magic happen

1. clone this repo 
2. compile the code
   ```
   gcc -o gifw gifw.c -lX11 -lm -lpthread
   ```
3. run it like you stole it
   ```
   ./gifw path/to/your/awesome.gif [stretch|center|tile]
   ```

 or run it with the makefile

 cd into the directory
 ```make```
 ```sudo make install```

set this in your xinitrc file to run on startup.

 ```
 .xinitrc

 gifw /home/user/wallpapers/avd.gif stretch &
```

## usage

```
./gifw <animated-gif-file> [stretch|center|tile]
```

- `stretch`: for when you want your gif to go to the gym
- `center`: for the indecisive middle-child gif
- `tile`: for when one gif just isn't enough

## requirements

- a computer
- X11
