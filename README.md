# wlines

Dynamic menu for Windows - inspired by the [suckless](https://suckless.org/) [dmenu](https://tools.suckless.org/dmenu/).

### Download

See the [latest release](https://github.com/JerwuQu/wlines/releases/latest).

### Usage

Menu entries are passed to wlines through stdin. After the user has made a choice, the result is sent out through stdout.

Running `printf 'hello\nworld\n:)' | wlines.exe` (or ``"hello`nworld`n:)" | wlines.exe`` if using PowerShell) would bring up this prompt:

![image](https://user-images.githubusercontent.com/3710677/112474131-85c37e80-8d6f-11eb-81df-44c21b19e6a8.png)

The user can then filter by typing in the textbox:

![image](https://user-images.githubusercontent.com/3710677/112474199-97a52180-8d6f-11eb-85b3-b700b342b940.png)

The menu style and behavior can be customized through command-line arguments. Run `wlines -h` for a list of these.

wlines by itself doesn't do much. The power comes through using scripts that talk to it. suckless has [a list of examples of scripts that can be used with dmenu](https://tools.suckless.org/dmenu/scripts/).

[Dave Davenport's](https://github.com/DaveDavenport) [rofi](https://github.com/DaveDavenport/rofi) (an alternative to dmenu) also has [such a list](https://github.com/DaveDavenport/rofi/wiki/User-scripts).

### Build steps

In a MinGW environment you can simply run `make` (e.g. `x86_64-w64-mingw32-make`).

See [the Github Actions Workflow](https://github.com/JerwuQu/wlines/blob/master/.github/workflows/build.yml) for a complete example.

### License

This project is licensed under the GNU General Public License v3.0. See LICENSE for more details.
