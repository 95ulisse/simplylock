# SimplyLock [![Build Status](https://travis-ci.org/95ulisse/simplylock.svg?branch=master)](https://travis-ci.org/95ulisse/simplylock)

`SimplyLock` allows you restrict physical access to your Linux box by locking all virtual terminals.

To lock your computer just call:

```
simplylock
```

And you're done. Unlock your computer by pressing *Enter* and then typing your password.

Note that `SimplyLock` uses **PAM** for authentication, it makes no assumptions on the existence
of passwords or any other authentication mechanism. To customize `SimplyLock` behaviour, edit
`/etc/pam.d/simplylock` (or the equivalent for your distribution).

## Which users can unlock?

**The root user can always unlock.**

But he isn't the only one. You can specify a list of users allowed to unlock using the `-u` option,
or if you called `SimplyLock` without the `-u` option, you (the caller) will be able to unlock.

If more than one user is allowed to unlock, you can press `Ctrl+C` before authentication to
select from the list of allowed users.

## Can I use SimplyLock to automatically lock my pc when I suspend it?

If you use systemd, adding a new unit is enough:

```
[Unit]
Description=Lock with SimplyLock
Before=sleep.target

[Service]
Type=forking
ExecStart=/bin/simplylock

[Install]
WantedBy=sleep.target
```

Save this unit to `/etc/systemd/system/simplylock.service` and issue a `systemctl daemon-reload`
to make sure that the changes are reloaded. Enable the unit with `systemctl enable simplylock`.

Now, every time you suspend through `systemctl suspend`, when you resume, your pc will be locked.

Note that if you use this exact unit, **only root will be able to unlock at resume**.
Use the `-u` option to list other users that can unlock.

## Installation

If you use Arch Linux, SimplyLock is easily available from the [AUR](https://aur.archlinux.org/packages/simplylock-git/):

```
$ yaourt -S simplylock-git
```

If you don't use Arch, you can always compile SimplyLock from source.

## Compile from source

To compile SimplyLock from source, you will need PAM and MagickWand headers, so install the package
for your distribution. If you use Debian, for example, you can install the following packages:

```
# apt-get install libpam0g-dev libmagickwand-dev
```

For Arch Linux:

```
# pacman -S pam imagemagick
```

Now, compile and install SimplyLock with the following commands:

```
$ make
# make install
```

Note that `make install` will place the binary in the `/usr/bin` directory, and will give it
**root ownership and set the setuid bit**, so that everyone can use SimplyLock.

## Background image

Optionally, you can add a background image to your lock screen. To do so, pass the path to
the image to the `-b / --background` option:

```
simplylock -b /home/user/Pictures/lock.jpg
```

By default, the image is resized and centered to the screen. To change this behaviour,
use the `--background-fill` option.

This feature requires the **Linux framebuffer**: if `/dev/fb0` is not available,
use the `--fbdev` option to point to the correct framebuffer device.

**Note**: this is still preliminary support. Expect glitches and bugs.

## Usage

```
Usage: simplylock [-slkdqhv] [-u users] [-m message] [-b path]

-s, --no-sysreq              Keep sysrequests enabled.
-l, --no-lock                Do not lock terminal switching.
-k, --no-kernel-messages     Do not mute kernel messages while the console is locked.
-u, --users users            Comma separated list of users allowed to unlock.
                             Note that the root user will always be able to unlock.
-m, --message message        Display the given message instead of the default one.
-d, --dark                   Dark mode: switch off the screen after locking.
-q, --quick                  Quick mode: do not wait for enter to be pressed to unlock.

-b, --background             Set background image.
    --background-fill        Background fill mode. Available values:
                             - center: center the image without resizing it.
                             - stretch: stretch the image to fill all the available space.
                             - resize: like stretch, but keeps image proportions. (default)
    --fbdev                  Path to the framebuffer device to use to draw the background.

-h, --help                   Display this help text.
-v, --version                Display version information.
```

## License

See `LICENSE` file in the root directory.
