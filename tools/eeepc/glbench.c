/*
 * glbench: immediate mode OpenGL, as Quake-engine games draw, timed and checksummed.
 *
 * It draws FRAMES frames of a fixed scene into a 256x256 window: QUADS textured, coloured quads
 * per frame through glBegin/glColor4f/glTexCoord2f(v)/glVertex3f(v)/glEnd, with a glBindTexture
 * between quads as a game does between surfaces. Then it reads the last frame back with
 * glReadPixels and prints a checksum of it and the time per frame.
 *
 * The checksum must be the same with and without Wine 0157's batching:
 *
 *   WINE_OPENGL_BATCH=0 wine glbench.exe
 *   wine glbench.exe
 *
 * Built as a 32-bit program, since the cost being measured is WoW64's:
 *   i686-w64-mingw32-gcc -O2 -o glbench.exe glbench.c -lopengl32 -lgdi32
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

#define SIZE 256
#define QUADS 2000
#define FRAMES 100 /* or the first argument */

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static void draw_frame( GLuint *textures, int frame )
{
    int i;

    glClear( GL_COLOR_BUFFER_BIT );
    for (i = 0; i < QUADS; i++)
    {
        float x = (float)((i * 37 + frame) % 200) / 100.0f - 1.0f;
        float y = (float)((i * 53) % 200) / 100.0f - 1.0f;
        float s = 0.1f;
        GLfloat v[3] = { x + s, y + s, 0.0f }, t[2] = { 1.0f, 1.0f };

        glBindTexture( GL_TEXTURE_2D, textures[i % 2] );
        glBegin( GL_QUADS );
        glColor4f( (i % 7) / 7.0f, (i % 5) / 5.0f, (i % 3) / 3.0f, 1.0f );
        glTexCoord2f( 0.0f, 0.0f ); glVertex3f( x, y, 0.0f );
        glTexCoord2f( 1.0f, 0.0f ); glVertex3f( x + s, y, 0.0f );
        glTexCoord2fv( t );         glVertex3fv( v );
        glColor3ub( 255, 255, 255 );
        glTexCoord2f( 0.0f, 1.0f ); glVertex2f( x, y + s );
        glEnd();
    }
}

int main( int argc, char **argv )
{
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32 };
    WNDCLASSA wc = { 0 };
    LARGE_INTEGER freq, start, end;
    unsigned char texel[2][4 * 4 * 4], *pixels;
    unsigned int sum = 0;
    GLuint textures[2];
    HWND hwnd;
    HGLRC rc;
    HDC hdc;
    int i, frames = argc > 1 ? atoi( argv[1] ) : FRAMES;

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA( NULL );
    wc.lpszClassName = "glbench";
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "glbench", "glbench", WS_POPUP | WS_VISIBLE, 0, 0, SIZE, SIZE, 0, 0, wc.hInstance, 0 );
    hdc = GetDC( hwnd );
    SetPixelFormat( hdc, ChoosePixelFormat( hdc, &pfd ), &pfd );
    rc = wglCreateContext( hdc );
    if (!rc || !wglMakeCurrent( hdc, rc ))
    {
        fprintf( stderr, "no GL context\n" );
        return 1;
    }
    printf( "renderer: %s\n", (const char *)glGetString( GL_RENDERER ) );

    for (i = 0; i < (int)sizeof(texel[0]); i++)
    {
        texel[0][i] = (i * 29) & 0xff;
        texel[1][i] = (i * 83 + 7) & 0xff;
    }
    glGenTextures( 2, textures );
    for (i = 0; i < 2; i++)
    {
        glBindTexture( GL_TEXTURE_2D, textures[i] );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel[i] );
    }
    glEnable( GL_TEXTURE_2D );
    glViewport( 0, 0, SIZE, SIZE );

    draw_frame( textures, 0 ); /* warm up */
    glFinish();

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    for (i = 0; i < frames; i++)
    {
        draw_frame( textures, i );
        SwapBuffers( hdc );
    }
    glFinish();
    QueryPerformanceCounter( &end );

    /* the last frame again, into the back buffer, to read it */
    draw_frame( textures, frames - 1 );
    glFinish();
    pixels = malloc( SIZE * SIZE * 4 );
    glReadPixels( 0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
    for (i = 0; i < SIZE * SIZE * 4; i++) sum = sum * 31 + pixels[i];
    printf( "glGetError: %#x\n", glGetError() );
    printf( "checksum: %08x\n", sum );
    printf( "%.2f ms per frame (%d quads, %d GL calls a frame)\n",
            (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart / frames, QUADS, QUADS * 13 + 1 );

    wglMakeCurrent( NULL, NULL );
    wglDeleteContext( rc );
    ReleaseDC( hwnd, hdc );
    DestroyWindow( hwnd );
    return 0;
}
