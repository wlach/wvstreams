#include "wvtest.h"
#include "wvstring.h"
#include <unistd.h>

static int sequence = 0;


WVTEST_MAIN()
{
    WvString a, b;
    
    WVPASS(sequence++ == 0);
    
    WVPASS(a==NULL);
    WVFAIL(b==NULL); // should fail
    WVPASS(1);
    WVPASS(a==b);
    WVFAIL(a!=b);
    
    a.append(b);
    WVPASS(a==b);
    WVFAIL(a == b); // should fail
    WVFAIL(a != b);
    WVPASS(a == b);
    WVPASS(!sleep(1));
    WVFAIL(a != b);
    
    a.append("blah");
    WVPASS(a=="blah");
    WVPASS(a!=b);
}


WVTEST_MAIN()
{
    WVPASS(++sequence == 2);
    WVPASS("booga booga");
}


WVTEST_MAIN()
{
    WVFAIL(++sequence != 3);
    WVPASS("booga booga");
    WVPASS(sequence != 3); // should fail
}


int main()
{
    return WvTest::run_all();
}