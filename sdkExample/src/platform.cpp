//------------------------------------------ Includes ----------------------------------------------

#include "platform.h"

#if defined(OS_WINDOWS)
    #include "windows.h"
    #include <conio.h>
#elif defined(OS_UNIX)
    #include <unistd.h>
#else
    #error "Unsupported platform. Define OS_WINDOWS or OS_UNIX"
#endif

using namespace IslSdk;

#ifdef OS_WINDOWS
//--------------------------------------------------------------------------------------------------
void Platform::sleepMs(unsigned int ms)
{
    Sleep(ms);
}
//--------------------------------------------------------------------------------------------------
std::string Platform::getExePath(char* argv0)
{
    char path[256];
    _splitpath_s(argv0, 0, 0, &path[0], sizeof(path), 0, 0, 0, 0);
    return std::string(&path[0]);
}
//--------------------------------------------------------------------------------------------------
void Platform::setTerminalMode()
{
}
//--------------------------------------------------------------------------------------------------
int Platform::keyboardPressed()
{
    return _kbhit();
}
//--------------------------------------------------------------------------------------------------
int Platform::getKey()
{
    return _getch();
}
//--------------------------------------------------------------------------------------------------
#elif OS_UNIX
void resetTerminalMode();
//--------------------------------------------------------------------------------------------------
void Platform::sleepMs(unsigned int ms)
{
    usleep(ms * 1000);
}
//--------------------------------------------------------------------------------------------------
std::string Platform::getExePath(char* argv0)
{
    char* path = realpath(argv0, nullptr);
    if (path)
    {
        std::string pathStr(path);
        free(path);
        std::string::size_type pos = pathStr.find_last_of("\\/");
        return pathStr.substr(0, pos) + "/";
    }
    return "";
}
//--------------------------------------------------------------------------------------------------
void resetTerminalMode()
{
    system("stty cooked echo");
}
//--------------------------------------------------------------------------------------------------
void Platform::setTerminalMode()
{
    atexit(resetTerminalMode);
    system("stty raw -echo");
}
//--------------------------------------------------------------------------------------------------
int Platform::keyboardPressed()
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}
//--------------------------------------------------------------------------------------------------
int Platform::getKey()
{
    return getchar();
}
//--------------------------------------------------------------------------------------------------
#endif
