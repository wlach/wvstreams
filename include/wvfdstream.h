/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * Base class for streams built on Unix file descriptors.
 */ 
#ifndef __WVFDSTREAM_H
#define __WVFDSTREAM_H

#include "wvstream.h"

/**
 * Base class for streams built on Unix file descriptors.
 * <p>
 * WvFDStream distinguishes between read and write file descriptors
 * at creation time.  Additionally, the reading and writing halves
 * may be independently shut down by calling noread() or nowrite().
 * </p>
 */
class WvFDStream : public WvStream
{
protected:
    /**
     * The file descriptor for reading.
     */
    int rfd;

    /**
     * The file descriptor for writing.
     */
    int wfd;

    /**
     * Sets the file descriptor for both reading and writing.
     * Convenience method.
     */
    void setfd(int fd)
        { rfd = wfd = fd; }

public:
    /**
     * Creates a WvStream from an existing file descriptor.
     * @param rwfd the file descriptor for reading and writing
     */
    WvFDStream(int rwfd = -1);
    
    /**
     * Creates a WvStream from two existing file descriptors.
     * <p>
     * The file decriptors may be the same.
     * </p>
     * @param rfd the file descriptor for reading
     * @param wfd the file descriptor for writing
     */
    WvFDStream(int rfd, int wfd);

    /**
     * Destroys the stream and invokes close().
     */
    virtual ~WvFDStream();

    /**
     * Returns the Unix file descriptor for reading from this stream.
     * @return the file descriptor, or -1 if none
     */
    int getrfd() const
        { return rfd; }
    
    /**
     * Returns the Unix file descriptor for writing to this stream.
     * @return the file descriptor, or -1 if none
     */
    int getwfd() const
        { return wfd; }

    /**
     * Returns the Unix file descriptor for reading and writing.
     * <p>
     * Asserts that the file descriptors for reading and writing
     * are the same before returning.
     * <p>
     * @return the file descriptor, or -1 if none
     */
    int getfd() const
    {
        assert(rfd == wfd);
        return rfd;
    }

    /**
     * Shuts down the reading side of this stream.
     * Subsequent calls to read() will fail.
     */
    void noread();

    /**
     * Shuts down the reading side of this stream.
     * Subsequent calls to write() will fail.
     */
    void nowrite();

    /***** Overridden members *****/
    
    /**
     * Closes the file descriptors.
     * <p>
     * If it is undesirable for the file descriptors to be closed by
     * this stream, duplicate the file descriptors using dup() before
     * creating the stream.
     * </p>
     */
    virtual void close();
    virtual bool isok() const;
    virtual size_t uread(void *buf, size_t count);
    virtual size_t uwrite(const void *buf, size_t count);
    virtual bool pre_select(SelectInfo &si);
    virtual bool post_select(SelectInfo &si);
};

#endif // __WVFDSTREAM_H
