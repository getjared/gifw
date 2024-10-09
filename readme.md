# gifw.c

## what's this wizardry?

this little c program takes your favorite gif and plasters it all over your desktop. why? because i can..

![](https://github.com/getjared/gifw/blob/main/gifw-desk.gif)


## issues. .
this was entirely made to work for x11, not wayland (though i do intend on adding xwayland later on)
the best way to actually use this is on a new setup, anything that changes the root window is going to interfere
with this little tool (consider it not finished)

## make it. .
```   
1. clone this repo 
2. compile the code

   git clone https://github.com/getjared/gifw.git
   cd gifw
   gcc -o gifw gifw.c -lX11 -lm -lpthread
   
3. run it like you stole it
   
   ./gifw path/to/your/awesome.gif [stretch|center|tile]

if you want to install it in order to run on start up
clone the repo
cd gifw
make
sudo make install

this will install to the /usr/local/bin/gifw
then just add it to your .xinitrc file
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
