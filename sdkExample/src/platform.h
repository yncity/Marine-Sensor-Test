#ifndef PLATFORM_H_
#define PLATFORM_H_

//------------------------------------------ Includes ----------------------------------------------

#include <string>

//--------------------------------------- Class Definition -----------------------------------------

namespace IslSdk
{
    namespace Platform
    {
        void sleepMs(unsigned int ms);
        std::string getExePath(char* argv0);
        void setTerminalMode();
        int keyboardPressed();
        int getKey();
    }
}
//--------------------------------------------------------------------------------------------------
#endif
