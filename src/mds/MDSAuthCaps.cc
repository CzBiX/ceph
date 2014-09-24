// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */


#include <errno.h>

#include "MDSAuthCaps.h"

/**
 * Simple parser for the three possible caps a client can have:
 * 'allow' -- legacy format, equivalent to 'rw'
 * 'allow r' -- readonly filesystem access
 * 'allow rw' -- read/write filesystem access
 * 'allow *' -- read/write filesystem access and `tell` commands
 *
 * @return 0 on success, else negative error code
 */
int MDSAuthCaps::parse(const std::string &str)
{
    if (str == "allow") {
    } else if (str == "allow r") {
        write = false;
        tell = false;
    } else if (str == "allow rw") {
        write = true;
        tell = false;
    } else if (str == "allow *") {
        write = true;
        tell = true;
    } else {
        return -EINVAL;
    }

    return 0;
}

