#include "wvdbusconn.h"
#include "wvdbuslistener.h"
#include "wvdbusserver.h"
#include "wvfileutils.h"
#include "wvfork.h"
#include "wvtest.h"
#include "wvloopback.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>


static int replies_received = 0;

static void reply_received(WvDBusConn &conn, WvDBusMsg &msg,
			   WvString foo, WvError err)
{
    fprintf(stderr, "wow! foo called! (%s)\n", foo.cstr());
    replies_received++;
}

static int messages_received = 0;

static void msg_received(WvDBusConn &conn, WvDBusMsg &msg,
			 WvString arg1, WvError err)
{
    fprintf(stderr, "Message received, loud and clear.\n");
    if (!err.isok())
	fprintf(stderr, "Error was: '%s'\n", err.errstr().cstr());
    messages_received++;
    msg.reply().append(WvString("baz %s", arg1)).send(conn);
}


class TestDBusServer
{
public:
    WvString moniker;
    pid_t parent, child;
    WvLoopback loop;
    
    TestDBusServer()
    {
	signal(SIGPIPE, SIG_IGN);
	fprintf(stderr, "Creating a test DBus server.\n");
	
	parent = getpid();
	child = wvfork(loop.getrfd(), loop.getwfd());
	if (child == 0)
	    do_child(); // never returns
	WVPASS(child >= 0);
	
	moniker = loop.getline(-1);
	fprintf(stderr, "Server address is '%s'\n", moniker.cstr());
    }
    
    ~TestDBusServer()
    {
	fprintf(stderr, "Killing test server.\n");
	kill(child, 15);
	pid_t rv;
	while ((rv = waitpid(child, NULL, 0)) != child)
	{
	    // in case a signal is in the process of being delivered..
	    if (rv == -1 && errno != EINTR)
		break;
	}
	WVPASS(rv == child);
    }
    
    void do_child()
    {
	WvString smoniker("unix:tmpdir=%s.dir",
			 wvtmpfilename("wvdbus-sock-"));
	WvDBusServer server(smoniker);
	
	loop.print("%s\n", server.get_addr());
	
	WvIStreamList::globallist.append(&server, false);
	while (server.isok())
	{
	    WvIStreamList::globallist.runonce(1000);
	    if (kill(parent, 0) < 0) break;
	}
	fprintf(stderr, "Server process terminating.\n");
	_exit(0);
    }
};


WVTEST_MAIN("basics")
{
}


WVTEST_MAIN("fancy listeners")
{
    TestDBusServer serv;
    WvDBusConn conn1(serv.moniker);
    conn1.request_name("ca.nit.MySender");
    WvDBusConn conn2(serv.moniker);
    conn2.request_name("ca.nit.MyListener");
    WvDBusListener<WvString> *l = 
        new WvDBusListener<WvString>(&conn2, "bar", msg_received);
    conn2.add_method("ca.nit.foo", "/ca/nit/foo", l);

    WvDBusMsg msg("ca.nit.MyListener", "/ca/nit/foo", "ca.nit.foo", "bar");
    msg.append("bee");

    WvDBusListener<WvString> reply(&conn1, "/ca/nit/foo/bar", reply_received);
    
    conn1.send(msg, &reply, false);
    WvIStreamList::globallist.append(&conn1, false);
    WvIStreamList::globallist.append(&conn2, false);

    fprintf(stderr, "Spinning...\n");
    while (replies_received < 1 || messages_received < 1)
         WvIStreamList::globallist.runonce();

    WVPASSEQ(messages_received, 1);
    WVPASSEQ(replies_received, 1);

    conn1.close();
    conn2.close();

    WvIStreamList::globallist.zap();
}
