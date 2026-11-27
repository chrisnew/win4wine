/* ole-clipboard-thread: the OLE clipboard still works after the thread that used it first has ended.
 *   ole-clipboard-thread-test
 * Wine patch 0161. XP Explorer calls OleFlushClipboard when a folder window closes, and the
 * window's thread then ends. Every call should give 0. Without the patch, OleSetClipboard after
 * that thread gives 0x800401d0 (CLIPBRD_E_CANT_OPEN). The program empties the clipboard. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static int failed;

static void check( const char *what, HRESULT hr )
{
    printf( "%-40s 0x%08lx\n", what, (unsigned long)hr );
    if (hr != S_OK) failed = 1;
}

static DWORD WINAPI first_user( void *arg )
{
    (void)arg;
    check( "first thread: OleInitialize", OleInitialize( NULL ) );
    check( "first thread: OleFlushClipboard", OleFlushClipboard() );
    OleUninitialize();
    return 0;
}

int main( void )
{
    HANDLE thread;

    /* as in Explorer, another thread keeps OLE initialized in the process */
    check( "main thread: OleInitialize", OleInitialize( NULL ) );
    if (!(thread = CreateThread( NULL, 0, first_user, NULL, 0, NULL ))) return 2;
    WaitForSingleObject( thread, INFINITE );
    CloseHandle( thread );

    check( "main thread: OleSetClipboard", OleSetClipboard( NULL ) );
    check( "main thread: OleFlushClipboard", OleFlushClipboard() );
    OleUninitialize();
    puts( failed ? "FAIL" : "PASS" );
    return failed;
}
