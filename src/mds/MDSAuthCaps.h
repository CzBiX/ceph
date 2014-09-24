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


#ifndef MDS_AUTH_CAPS_H
#define MDS_AUTH_CAPS_H

#include <string>

class MDSAuthCaps
{
    protected:
    bool write;
    bool tell;

    public:
    int parse(const std::string &str);
    MDSAuthCaps() : write(false), tell(false) {}
    bool get_tell() const {return tell;}
};

#endif // MDS_AUTH_CAPS_H
