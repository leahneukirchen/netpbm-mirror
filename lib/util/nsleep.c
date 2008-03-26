#ifdef WIN32
  #include <windows.h>
  #include <process.h>
#else
  #include <unistd.h>
#endif

#include "nsleep.h"



void
sleepN(unsigned int const milliseconds) {

#ifdef WIN32
    SleepEx(milliseconds, TRUE);
#else

    /* We could use usleep() here if millisecond resolution is really
       important, but since Netpbm has no need for it today, we don't
       want to deal with the possibility that usleep() doesn't exist.
       08.08.01.
    */

    sleep((milliseconds + 999)/1000);
#endif
}
