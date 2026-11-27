/* uniscribe-thread: threads that meet in Uniscribe's font cache at the same time.
 *   uniscribe-thread-test
 * Wine patch 0165. XP Explorer's folder windows each have a thread, and when a drive goes they
 * redraw at once. Every thread makes a SCRIPT_CACHE for the same few fonts and frees it again,
 * so that two often make the same one at the same time. Without the patch the program crashes or
 * hangs within seconds: Wine takes the other thread's cache off its list and frees its own,
 * which stays on it. */
#include <windows.h>
#include <usp10.h>
#include <stdio.h>

#define THREADS 8
#define ROUNDS 20000
#define FONTS 4

static HFONT fonts[FONTS];
static LONG errors, crashed;

static DWORD WINAPI worker( void *arg )
{
    HDC dc = CreateCompatibleDC( NULL );
    int i;

    for (i = 0; i < ROUNDS; i++)
    {
        SCRIPT_CACHE first = NULL, second = NULL;
        long height;

        SelectObject( dc, fonts[(i + (INT_PTR)arg / THREADS) % FONTS] );
        if (ScriptCacheGetHeight( dc, &first, &height ) != S_OK || height <= 0) InterlockedIncrement( &errors );
        if (ScriptCacheGetHeight( dc, &second, &height ) != S_OK) InterlockedIncrement( &errors );
        ScriptFreeCache( &first );
        ScriptFreeCache( &second );
    }
    DeleteDC( dc );
    return 0;
}

static LONG WINAPI report_crash( EXCEPTION_POINTERS *info )
{
    if (!InterlockedExchange( &crashed, 1 ))
        printf( "exception 0x%08lx at %p\nFAIL\n", info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress );
    fflush( stdout );
    TerminateProcess( GetCurrentProcess(), 1 );
    return EXCEPTION_EXECUTE_HANDLER;
}

int main( void )
{
    HANDLE threads[THREADS];
    int i;

    SetUnhandledExceptionFilter( report_crash );
    for (i = 0; i < FONTS; i++) fonts[i] = CreateFontA( -11 - i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "Tahoma" );
    for (i = 0; i < THREADS; i++)
        if (!(threads[i] = CreateThread( NULL, 0, worker, (void *)(INT_PTR)i, 0, NULL ))) return 2;
    if (WaitForMultipleObjects( THREADS, threads, TRUE, 120000 ) == WAIT_TIMEOUT)
    {
        puts( "the threads did not finish within 120 s\nFAIL" );
        fflush( stdout );
        TerminateProcess( GetCurrentProcess(), 1 );
    }
    printf( "%d threads, %d rounds each, %ld errors\n%s\n", THREADS, ROUNDS, errors, errors ? "FAIL" : "PASS" );
    return errors != 0;
}
