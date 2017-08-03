// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"

#include <assert.h>
#include <math.h>
#include <vector>

#include "GLEW/glew.h"
#include "GLEW/glew.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <vlc_common.h>
#include <vlc_fourcc.h>
extern "C"
{
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <vlc/vlc.h>
#include <vlc_fs.h>

#include <va/va_drmcommon.h>
#include <va/va_drm.h>
#include <va/va_x11.h>
#include <fcntl.h>
}

static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static float g_Time;

static GLuint g_TextureHandle = (size_t) NULL;
static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;
static int   g_TextureRowPitch = 0;

static unsigned char* vlcVideoFramePtr = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

GLuint bufferTexture;
GLuint fboId;


VADisplay dpy;
VANativeDisplay native;

Display *display;
VADisplay vaGLXdisplay;

GLXContext unityGLContext;
GLXContext helperGLContext;

void debugImage(unsigned char * beginning, int nbPixels)
{
  for (unsigned char *ptr = beginning; ptr < beginning + nbPixels; ptr++)
    {
      fprintf (stderr, "%x", *ptr);
    }
  fprintf (stderr, "\n");
}

void debugTexture(GLuint texture, int nbPixels)
{
  unsigned char *check = (unsigned char *) malloc (g_TextureWidth*g_TextureHeight*4);
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, check);
  debugImage(check, nbPixels);
}

void format_cb (void **opaque, char *chroma,
		unsigned *width, unsigned *height,
		unsigned *x_offset, unsigned *y_offset,
		unsigned *visible_width,
		unsigned *visible_height,
		void **device)
{
  memcpy(chroma, "VAOP", 4);
  fprintf(stderr, "Chroma %s", chroma);
  // *width = g_TextureWidth;
  // *height = g_TextureHeight;
  // *x_offset = 0;
  // *y_offset = 0;
  // *visible_width = g_TextureWidth;
  // *visible_height = g_TextureHeight;
  *device = dpy;
  fprintf(stderr, "VADisplay: %p, device: %p\n", dpy, *device);
}

VAStatus status = 0;
VASurfaceID surface;
VAImage image;
void *glx_surface = NULL;

void cleanup_cb (void *opaque)
{
  vlc_close((intptr_t) native);
}

void display_cb (void *opaque, void *const *planes,
				unsigned *pitches, unsigned *lines)
{
  fprintf(stderr, "\nIn VLC's cb :\n");

  // Lock the mutex to ensure data safeness
  pthread_mutex_lock(&mutex);

  glXMakeCurrent (display, (size_t) NULL, helperGLContext);

  status = vaDeriveImage(dpy, surface, &image);
  if (status != VA_STATUS_SUCCESS)
    fprintf(stderr, "VA-API: vaDeriveImage failed\n");


  /********* RECUPERATION EN CPU */
  unsigned char* buffer = 0;
  status = vaMapBuffer(dpy, image.buf, (void **)&buffer);
  if (status != VA_STATUS_SUCCESS)
    fprintf(stderr, "VA-API: vaMapBuffer failed\n");

  free(vlcVideoFramePtr);
  vlcVideoFramePtr = (unsigned char *) malloc (g_TextureWidth * g_TextureHeight * 4);
  for(unsigned char * i = vlcVideoFramePtr; i < vlcVideoFramePtr + g_TextureWidth * g_TextureHeight * 4; i+=4) { *i = 0xCC; }

  memcpy(vlcVideoFramePtr, (void *) buffer, g_TextureWidth * g_TextureHeight);

  /****** Copy to intermediate buffer (simulate vbridge) */
  // fprintf(stderr, "\n[LIBVLC] Rendering to intermediate buffer :\n");
  glBindTexture(GL_TEXTURE_2D, bufferTexture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_TextureWidth, g_TextureHeight, GL_RGB, GL_UNSIGNED_BYTE, vlcVideoFramePtr);

  // CLEANING UP
  status = vaUnmapBuffer(dpy, image.buf);
  if (status != VA_STATUS_SUCCESS)
    fprintf(stderr, "VA-API: vaUnmapBuffer failed\n");

  status = vaDestroyImage(dpy, image.image_id);
  if (status != VA_STATUS_SUCCESS)
    fprintf(stderr, "VA-API: vaDestroyBuffer failed\n");

  // Release datas
  pthread_mutex_unlock(&mutex);
}

// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity (float t) { g_Time = t; }

// --------------------------------------------------------------------------
// ModifyTexturePixels, an example function we export which is called by one of the scripts.
static void
ModifyTexturePixels ()
{
  // Lock mutex to ensure all datas had been written
  pthread_mutex_lock(&mutex);

  fprintf(stderr, "\n[LIBVLC] Unity's CB :\n");

  /****** in-GPU copy from intermediate buffer to Unity's */
  fprintf(stderr, "\n[LIBVLC] In-GPU Copy :\n");

  // We need to bind to a specific FBO to copy the texture
  glBindFramebuffer(GL_FRAMEBUFFER, fboId);

  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bufferTexture, 0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindTexture(GL_TEXTURE_2D, g_TextureHandle);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_TextureWidth, g_TextureHeight);


  // Rebing to default FBO
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Release datas
  pthread_mutex_unlock(&mutex);
}


extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
SetTextureFromUnity (void* textureHandle, int w, int h)
{
  // A script calls this at initialization time; just remember the texture pointer here.
  // Will update texture pixels each frame from the plugin rendering event (texture update
  // needs to happen on the rendering thread).
  g_TextureHandle = (GLuint)(size_t) textureHandle;
  g_TextureWidth = w;
  g_TextureHeight = h;
}

libvlc_instance_t * inst;
libvlc_media_player_t *mp;
libvlc_media_t *m;


/** LibVLC's API function exported to Unity
 *
 * Every following functions will be exported to. Unity We have to
 * redeclare the LibVLC's function for the keyword
 * UNITY_INTERFACE_EXPORT and UNITY_INTERFACE_API
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
launchVLC (char *videoURL)
{  

  /***** Create VAAPI context and stuff */
    static const char *const drm_device_paths[] = {
    "/dev/dri/renderD128",
    "/dev/dri/card0"
  };

  for (size_t i = 0; i < ARRAY_SIZE(drm_device_paths); i++)
    {
      int drm_fd = vlc_open(drm_device_paths[i], O_RDWR);
      if (drm_fd == -1)
	continue;

      fprintf(stderr, "Trying device: %s\n", drm_device_paths[i]);
      dpy = vaGetDisplayDRM(drm_fd);
      if (dpy)
	{
	  native = (VANativeDisplay *)(intptr_t) drm_fd;
	  fprintf(stderr, "DPY: %p, Native: %p\n", dpy, native);
	  break;
	}
      else
	vlc_close(drm_fd);
    }

 
  /***** Create GL stuff */

  // Get render OpenGL context
  unityGLContext = glXGetCurrentContext();

  // Create an FBO for copy
  glGenFramebuffers(1, &fboId);

  // Create a helper OpenGL (EGL) context

  display = XOpenDisplay(NULL);
  vaGLXdisplay = vaGetDisplay (display);

  // Create a helper OpenGL (GLX) context
  int scrnum = DefaultScreen (display);
  int attrib[] = { GLX_RGBA,
  		   GLX_RED_SIZE, 1,
  		   GLX_GREEN_SIZE, 1,
  		   GLX_BLUE_SIZE, 1,
  		   GLX_DOUBLEBUFFER, None };
  XVisualInfo* visinfo = glXChooseVisual (display, scrnum, attrib);
  if (!visinfo)
    fprintf(stderr, "Error: couldn't get an RGB, Double-buffered visual\n");

  // Create a context with unityGLContext as shared lists
  helperGLContext = glXCreateContext(display, visinfo, unityGLContext, true);
  // glXMakeCurrent (display, (size_t) NULL, helperGLContext);

  // Create a buffer for texture
  glEnable(GL_TEXTURE_2D);
  glGenTextures(1, &bufferTexture);
  glBindTexture(GL_TEXTURE_2D, bufferTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_TextureWidth, g_TextureHeight, 0, GL_BGRA,
  	       GL_UNSIGNED_BYTE, NULL);
  glBindTexture(GL_TEXTURE_2D, 0);

  // Create a mutex, to share data between LibVLC's callback and Unity
  fprintf (stderr, "[LIBVLC] Instantiating mutex...\n");
  if (pthread_mutex_init (&mutex, NULL) != 0)
    fprintf(stderr, "[LIBVLC] Mutex init failed\n");

  const char * const vlc_args[] = {
    "--verbose=3",};

  // Create an instance of LibVLC
  fprintf(stderr, "[LIBVLC] Instantiating LibLVC : %s...\n", libvlc_get_version());
  inst = libvlc_new (sizeof(vlc_args) / sizeof(vlc_args[0]), vlc_args);
  if (inst == NULL)
    fprintf(stderr, "[LIBVLC] Error instantiating LibVLC\n");

  // Create a new item
  fprintf(stderr, "[LIBVLC] Video url : %s\n", videoURL);
  m = libvlc_media_new_location (inst, videoURL);
  if (m == NULL)
    fprintf(stderr, "[LIBVLC] Error initializing media\n");

  libvlc_media_add_option(m, ":avcodec-hw=vaapi");
  libvlc_media_add_option(m, "-vvv");

  mp = libvlc_media_player_new_from_media (m);
  if (mp == NULL)
    fprintf(stderr, "[LIBVLC] Error initializing media player\n");

  // Release the media and the player since we don't need them anymore
  libvlc_media_release (m);
  libvlc_release(inst);

  // Instantiate a first buffer from frame, because our callbacks will
  // try to free it
  vlcVideoFramePtr = (unsigned char *) malloc (g_TextureWidth * g_TextureHeight * 4);

  // Set callbacks for activating vmem. Vmem let us handle video output
  // separatly from LibVLC classical way
  libvlc_media_player_set_vbridge_callbacks(mp, format_cb, cleanup_cb, NULL, display_cb, NULL);

  // Play the media
  libvlc_media_player_play (mp);
}	

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
stopVLC () {
  // Stop playing
  libvlc_media_player_stop (mp);
 
  // Free the media_player
  libvlc_media_player_release (mp);

  fprintf(stderr, "[CUSTOMVLC] VLC STOPPED\n");
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
playPauseVLC ()
{
  // Pause playing
  libvlc_media_player_pause (mp);
 
  fprintf(stderr, "[CUSTOMVLC] VLC PAUSE TOGGLED\n");
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
pauseVLC ()
{
  // Paused playing
  libvlc_media_player_pause (mp);
 
  fprintf(stderr, "[CUSTOMVLC] VLC PAUSED\n");
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getLengthVLC ()
{
  fprintf(stderr, "[CUSTOMVLC] Length %d\n", (int) libvlc_media_player_get_length (mp));
  return (int) libvlc_media_player_get_length (mp);
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getTimeVLC ()
{
  return (int) libvlc_media_player_get_time (mp);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
setTimeVLC (int pos)
{
    libvlc_media_player_set_time (mp, pos);
}


/** Unity API function
 *
 * Following functions are needed for integration into Unity's API.
 * UnitySetInterfaces
 */

// --------------------------------------------------------------------------
//  UnitySetInterfaces

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
  s_UnityInterfaces = unityInterfaces;
  s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
  s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
	
  // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
  OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
  s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

#if UNITY_WEBGL
typedef void	(UNITY_INTERFACE_API * PluginLoadFunc)(IUnityInterfaces* unityInterfaces);
typedef void	(UNITY_INTERFACE_API * PluginUnloadFunc)();

extern "C" void	UnityRegisterRenderingPlugin(PluginLoadFunc loadPlugin, PluginUnloadFunc unloadPlugin);

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RegisterPlugin()
{
  UnityRegisterRenderingPlugin(UnityPluginLoad, UnityPluginUnload);
}
#endif


// --------------------------------------------------------------------------
// GraphicsDeviceEvent

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
  // Create graphics API implementation upon initialization
  if (eventType == kUnityGfxDeviceEventInitialize)
    {
      assert(s_CurrentAPI == NULL);
      s_DeviceType = s_Graphics->GetRenderer();
      s_CurrentAPI = CreateRenderAPI(s_DeviceType);
    }

  // Let the implementation process the device related events
  if (s_CurrentAPI)
    {
      s_CurrentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
    }

  // Cleanup graphics API implementation upon shutdown
  if (eventType == kUnityGfxDeviceEventShutdown)
    {
      delete s_CurrentAPI;
      s_CurrentAPI = NULL;
      s_DeviceType = kUnityGfxRendererNull;
    }
}


// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
  // Unknown / unsupported graphics device type? Do nothing
  if (s_CurrentAPI == NULL)
    return;

  ModifyTexturePixels();
}


// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
  return OnRenderEvent;
}

