/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * One more attempt at making a decent stream for Linux /dev/dsp.
 * See wvdsp.h.
 */
#include "wvdsp.h"
#include "wvfork.h"
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <fcntl.h>
#include <sched.h>

static const char *AUDIO_DEVICE = "/dev/dsp";

static int msec_lat(int frags, int frag_bits, int srate)
{
    return frags * (1<<frag_bits) * 1000 / srate;
}


WvDsp::WvDsp(int msec_latency, int srate, int bits, bool stereo,
	     bool readable, bool writable)
    : log("DSP", WvLog::Debug), rbuf(102400), wbuf(102400)
{
    int mode = 0;
    
    assert(msec_latency >= 0);
    assert(srate >= 8000);
    assert(srate <= 48000);
    assert(bits == 8 || bits == 16);
    assert(readable || writable);
    
    if (readable && writable)
	mode = O_RDWR;
    else if (readable)
	mode = O_RDONLY;
    else if (writable)
	mode = O_WRONLY;

    // we do O_NONBLOCK in case someone is currently using the dsp
    // device and the particular one we're using can't be shared.
    if ((fd = open(AUDIO_DEVICE, mode | O_NONBLOCK)) < 0)
    {
    	seterr(errno);
	return;
    }
    
    // now get rid of O_NONBLOCK, so write() and read() on our fd don't return
    // until we get our data.  Since select() doesn't work anyway with some
    // drivers, we'll have to cheat.
    fcntl(fd, F_SETFL, mode);
    
    // set the fragment size appropriately for the desired latency
    num_frags = 5;
    int frag_size_bits = 7; // log2 of fragment size
    int lat;
    if (msec_latency > 1000)
	msec_latency = 1000; // don't be _too_ ridiculous...
    while (msec_latency > (lat = msec_lat(num_frags, frag_size_bits, srate)))
    {
	if (frag_size_bits < 14 && msec_latency >= 2*lat)
	    frag_size_bits++;
	else
	    num_frags++;
    }
    
    log("Latency will be about %s ms.\n", lat);
    
    frag_size = (1 << frag_size_bits);
    if (!setioctl(SNDCTL_DSP_SETFRAGMENT, (num_frags << 16) | frag_size_bits))
	seterr("can't set frag size!");
    
    if (bits == 16)
    {
	if (!setioctl(SNDCTL_DSP_SETFMT, AFMT_S16_NE))
	    seterr("can't set sample size!");
    }
    else if (bits == 8)
    {
	if (!setioctl(SNDCTL_DSP_SETFMT, AFMT_S8))
	    seterr("can't set sample size!");
    }
	
    if (!setioctl(SNDCTL_DSP_CHANNELS, stereo ? 2 : 1))
	seterr("can't set number of channels!");
    
    if (!setioctl(SNDCTL_DSP_SPEED, srate))
	seterr("can't set sampling rate!");

    // in fact, the OSS docs say we're not allowed to have separate processes
    // doing reads and writes at the same time.  Unfortunately, ALSA seems
    // to _need_ that for decent real-time performance.  But you can toggle
    // it here :)
    if (1)
    {
	// start the reader process
	subproc(true, false);
	
	// start the writer process
	subproc(false, true);
    }
    else
    {
	// start the read/writer process
	subproc(true, true);
    }
    
    rloop.nowrite();
    wloop.noread();
    //realtime(); // actually necessary, but a bit dangerous...
}


WvDsp::~WvDsp()
{
    close();
}


bool WvDsp::pre_select(SelectInfo &si)
{
    bool ret = false;
    size_t rleft = rbuf.used(), wleft = wbuf.used();
    
    if (rleft > 2*frag_size)
	log("read circle is filling! (%s = %s)\n", rleft, rleft/frag_size);
    if (wleft > 3*frag_size)
	log("write circle is filling! (%s = %s; %s)\n", 
	    wleft, wleft/frag_size, ospace());
    
    if (si.wants.readable)
    {
	rloop.drain();
	if (rbuf.used())
	    return true;
	else
	    ret |= rloop.pre_select(si);
    }
    
    if (si.wants.writable)
	return true;
    
    return ret;
}


bool WvDsp::post_select(SelectInfo &si)
{
    bool ret = false;
    
    if (si.wants.readable)
    {
	if (rbuf.used())
	    return true;
	else
	    ret |= rloop.post_select(si);
    }
    
    return ret;
}


size_t WvDsp::uread(void *buf, size_t len)
{
    size_t avail = rbuf.used();
    
    if (avail < len)
	len = avail;
    
    len = rbuf.get(buf, len);
    return len;
}


size_t WvDsp::uwrite(const void *buf, size_t len)
{
    size_t howmuch = wbuf.left();
    
    if (howmuch > len)
	howmuch = len;
    
    wbuf.put(buf, len);
    wloop.write("", 1);
    
    return len; // never let WvStreams buffer anything
}


bool WvDsp::isok() const
{
    return (fd >= 0);
}


void WvDsp::close()
{
    if (fd >= 0)
	::close(fd);
    fd = -1;
}


bool WvDsp::setioctl(int ctl, int param)
{
    return ioctl(fd, ctl, &param) >= 0;
}


// set realtime scheduling priority
void WvDsp::realtime()
{
    if (1)
    {
	struct sched_param sch;
	memset(&sch, 0, sizeof(sch));
	sch.sched_priority = 1;
	if (sched_setscheduler(getpid(), SCHED_FIFO, &sch) < 0)
	    seterr("can't set scheduler priority!");
    }
}


void WvDsp::subproc(bool reading, bool writing)
{
    intTable fds(4);
    fds.add(new int(rloop.getrfd()), true);
    fds.add(new int(rloop.getwfd()), true);
    fds.add(new int(wloop.getrfd()), true);
    fds.add(new int(wloop.getwfd()), true);
    
    pid_t pid = wvfork(fds);
    if (pid < 0)
    {
	seterr(errno);
	return;
    }
    else if (pid > 0) // parent
	return;
    
    // otherwise, this is the child
    
    char buf[10240];
    size_t len;
    
    realtime();
    
    rloop.noread();
    wloop.nowrite();
    
    if (!reading)
	rloop.close();
    if (!writing)
	wloop.close();
    
    while (isok() && (rloop.isok() || wloop.isok()))
    {
	if (reading)
	{
	    len = do_uread(buf, sizeof(buf));
	    if (len)
	    {
		rbuf.put(buf, len);
		rloop.write("", 1);
	    }
	}
	
	if (writing)
	{
	    if (wbuf.used() || reading || wloop.select(-1))
	    {
		wloop.drain();
		
		len = wbuf.used();
		if (len > frag_size)
		    len = frag_size;
		len = wbuf.get(buf, len);
		do_uwrite(buf, len);
	    }
	}
    }
	
    _exit(0);
}


size_t WvDsp::ispace()
{
    audio_buf_info info;
    
    if (ioctl(fd, SNDCTL_DSP_GETISPACE, &info) < 0)
    {
	log("error in GETISPACE\n");
	return 0;
    }
    
    assert(info.fragsize == (int)frag_size);
    
    return info.fragments;
}


size_t WvDsp::ospace()
{
    audio_buf_info info;
    
    if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) < 0)
    {
	log("error in GETOSPACE\n");
	return 0;
    }
    
    return num_frags - info.fragments;
}


size_t WvDsp::do_uread(void *buf, size_t len)
{
    int i, i2;
    
    if (len > frag_size)
	len = frag_size;
    
    if ((i = ispace()) > 1)
    {
	i2 = i;
	while (i2-- > 1)
	{
	    char buf2[frag_size];
	    ::read(fd, buf2, frag_size);
	}
	log("inbuf is filling up! (%s waiting)\n", i);
    }
    
    // note: ALSA drivers sometimes read zero bytes even with stuff in the
    // buffer (sigh).  So that's not EOF in this case.
    int ret = ::read(fd, buf, len);
    if (ret < 0)
    {
	if (errno != EAGAIN)
	    seterr(errno);
	return 0;
    }
    
    if (ret && ret < (int)len && ret < (int)frag_size)
	log("inbuf underflow (%s/%s)!\n", ret, len);
    
    return ret;
}


size_t WvDsp::do_uwrite(const void *buf, size_t len)
{
    int o = ospace(), o2;
    
    if (o < 2)
    {
	o2 = o;
	while (o2++ < 2)
	{
	    char buf2[frag_size];
	    memset(buf2, 0, sizeof(buf2));
	    ::write(fd, buf2, frag_size);
	}
	log("outbuf is almost empty! (%s waiting)\n", o);
    }
    
    if (o >= (int)num_frags-1)
    {
	log("outbuf overflowing (%s): skipping write.\n", o);
	return len;
    }
    
    size_t ret = ::write(fd, buf, len);
    if (ret < 0)
    {
	if (errno != EAGAIN)
	    seterr(errno);
	return len; // avoid using WvStreams buffer
    }
    
    if (ret < len)
	log("outbuf overflow (%s/%s)!\n", ret, len);
    
    return len; // avoid using WvStreams buffer
}


