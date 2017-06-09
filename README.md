# SimplyLock [![Build Status](https://travis-ci.org/MrTiz9/simplylock.svg?branch=master)](https://travis-ci.org/MrTiz9/simplylock)

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

To compile SimplyLock from source, you will need PAM headers, so install the package for your
distribution. If you use Debian, for example, you can install the `libpam0g-dev` package:

```
# apt-get install libpam0g-dev
```

Now, compile and install SimplyLock with the following commands:

```
$ make
# make install
```

Note that `make install` will place the binary in the `/bin` directory, and will give it
**root ownership and set the setuid bit**, so that everyone can use SimplyLock.

## Usage

```
Usage: ./out/simplylock [-slkdqhv] [-u users] [-m message]

-s, --no-sysreq              Keep sysrequests enabled.
-l, --no-lock                Do not lock terminal switching.
-k, --no-kernel-messages     Do not mute kernel messages while the console is locked.
-u, --users users            Comma separated list of users allowed to unlock.
                             Note that the root user will always be able to unlock.
-c, --dont-clean-vt          Don't clean vt after wrong password.
-m, --message message        Display the given message instead of the default one.
-d, --dark                   Dark mode: switch off the screen after locking.
-q, --quick                  Quick mode: do not wait for enter to be pressed to unlock.

-h, --help                   Display this help text.
-v, --version                Display version information.
```

## License

See `LICENSE` file in the root directory.
