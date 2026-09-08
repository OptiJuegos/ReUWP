## ReUWP - Minecraft Bedrock 0.15.10/1.1.5 UWP to Win32 translation layer!!

i fucking hate uwp apps and the winrt standard & windows 10/11, so this is a translation of that APIs to Win32!!!
to run in a fucking good version of windows!! 

### how to use this

- gotto releases, download the zip with the reuwp.dll and reuwp_patch.exe
- unpack your mcbe 1.1.5 or 0.15.10 build
- drag and drop the Minecraft.Windows.exe or Minecraft.DX10.DX11.exe into reuwp_patch.exe
- copy reuwp.dll into the minecraft folder
- run Minecraft.Windows_reuwp.exe and that's it!!

### how to compile
- cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
- cmake --build build --config Release
- you need the msvc runtime!!! so get into download the 15gb crap of visual studio!! (or download an standalone msvc)
