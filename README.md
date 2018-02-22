Demo VLC Virtual Cinema
=======================

This Unity project is a demo on how to use LibVLC (the core of VLC
media player) in a Unity project in order to use its functionnality.

With LibVLC you can use every aspect of the VLC media player in your game.

This project is fully free and open-source, you're welcome to use it
to your needs, improve it, ...

# About the methods of video copying

Basically, I tried several method to copy the image of VLC to Unity's game.

- The first one was to copy in-CPU from VLC's offscreen array to Unity's texture, using the Unity's example of API. It uses D3D9 or GLX in background.
- The second one was to use vbridge's output module of VLC, which wasn't in VLC 2.x (but is in 3.x now, you won't have to recompile VLC from sources..) and use the same method a the first one. You can find this in branch *vbridge*.
- Diverse methods to try to use hardware acceleration was to make a copy directl in-GPU. I tried either VAAPI and FBOAPI. I abandoned these because I never managed to prevent the segfault outside the editor. You can find these in branch *FBOAPI* and *VAAPItoGLX*

# How to use

1. You need to compile the lib in he lib VlcUnityWrapper and put the resulting .dll (on windows) or .so (on Linux) in Assets/x86_64
2. Be sure you have the libvlc and libvlccore lib in folder where your OS can find them on runtime
3. Edit in Assets/UseRenderingPlugin.cs the paths to your videos
4. Just launch the Unity Player, it should play...

# How to compile Lib

If you want to change the way the rendering lib works, you'll have to recompile the lib.

You'll need a local version of VLC sources to compile against them, see here : https://wiki.videolan.org/VLC_Source_code/


## On Linux

The Makefile should be enough, be sure to change the path accordingly to your env !

## On Windows

In a nutshell, here are the steps to compile:
- Create a DLL project with your IDE in the dir VLC-Unity-Wrapper.
- Install VLC to get the dll, download VLC sources to get the header files. Configure your IDE to set up the header files, link dynamically to vlc, you will need to rename libvlc.dll to vlc.dll if you use MinGW-w64 because this soft is stupid.
- Download GLEW: https://sourceforge.net/projects/glew/files/glew/2.1.0/glew-2.1.0-win32.zip/download. Configure your IDE to set up the header files, link statically to glew32s.lib in Release/x64.
- Set the custom compiler variable *GLEW_STATIC* (use `-D GLEW_STATIC` flag with MinGW-w64/GCC)
- Finally, link to `glu32`, `opengl32` and `pthread`.

If you have problem with pthread linking/header, be sure that your compiler handle POSIX Threading lib. MinGW-w64 do by default.
Else, you can download the prebuilt pthread lib and setup their header and linking.