// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph distributed storage system
 *
 * Copyright (C) 2014 Cloudwatt <libre.licensing@cloudwatt.com>
 * Copyright (C) 2014 Red Hat <contact@redhat.com>
 *
 * Author: Loic Dachary <loic@dachary.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 * 
 */

#include <errno.h>
#include <vector>
#include <algorithm>

#include "common/strtol.h"
#include "ErasureCode.h"

int ErasureCode::minimum_to_decode(const set<int> &want_to_read,
                                   const set<int> &available_chunks,
                                   set<int> *minimum)
{
  if (includes(available_chunks.begin(), available_chunks.end(),
	       want_to_read.begin(), want_to_read.end())) {
    *minimum = want_to_read;
  } else {
    unsigned int k = get_data_chunk_count();
    if (available_chunks.size() < (unsigned)k)
      return -EIO;
    set<int>::iterator i;
    unsigned j;
    for (i = available_chunks.begin(), j = 0; j < (unsigned)k; ++i, j++)
      minimum->insert(*i);
  }
  return 0;
}

int ErasureCode::minimum_to_decode_with_cost(const set<int> &want_to_read,
                                             const map<int, int> &available,
                                             set<int> *minimum)
{
  set <int> available_chunks;
  for (map<int, int>::const_iterator i = available.begin();
       i != available.end();
       ++i)
    available_chunks.insert(i->first);
  return minimum_to_decode(want_to_read, available_chunks, minimum);
}

int ErasureCode::encode_prepare(const bufferlist &raw,
                                map<int, bufferlist> &encoded) const
{
  unsigned int k = get_data_chunk_count();
  unsigned int m = get_chunk_count() - k;
  unsigned blocksize = get_chunk_size(raw.length());
  unsigned pad_len = blocksize * k - raw.length();
  unsigned padded_chunks = k - raw.length() / blocksize;
  bufferlist prepared = raw;

  if (!prepared.is_aligned()) {
    // splice padded chunks off to make the rebuild faster
    if (padded_chunks)
      prepared.splice((k - padded_chunks) * blocksize,
                      padded_chunks * blocksize - pad_len);
    prepared.rebuild_aligned();
  }

  for (unsigned int i = 0; i < k - padded_chunks; i++) {
    int chunk_index = chunk_mapping.size() > 0 ? chunk_mapping[i] : i;
    bufferlist &chunk = encoded[chunk_index];
    chunk.substr_of(prepared, i * blocksize, blocksize);
  }
  if (padded_chunks) {
    unsigned remainder = raw.length() - (k - padded_chunks) * blocksize;
    bufferlist padded;
    bufferptr buf(buffer::create_aligned(padded_chunks * blocksize));

    raw.copy((k - padded_chunks) * blocksize, remainder, buf.c_str());
    buf.zero(remainder, pad_len);
    padded.push_back(buf);

    for (unsigned int i = k - padded_chunks; i < k; i++) {
      int chunk_index = chunk_mapping.size() > 0 ? chunk_mapping[i] : i;
      bufferlist &chunk = encoded[chunk_index];
      chunk.substr_of(padded, (i - (k - padded_chunks)) * blocksize, blocksize);
    }
  }
  for (unsigned int i = k; i < k + m; i++) {
    int chunk_index = chunk_mapping.size() > 0 ? chunk_mapping[i] : i;
    bufferlist &chunk = encoded[chunk_index];
    chunk.push_back(buffer::create_aligned(blocksize));
  }

  return 0;
}

int ErasureCode::encode(const set<int> &want_to_encode,
                        const bufferlist &in,
                        map<int, bufferlist> *encoded)
{
  unsigned int k = get_data_chunk_count();
  unsigned int m = get_chunk_count() - k;
  bufferlist out;
  int err = encode_prepare(in, *encoded);
  if (err)
    return err;
  encode_chunks(want_to_encode, encoded);
  for (unsigned int i = 0; i < k + m; i++) {
    if (want_to_encode.count(i) == 0)
      encoded->erase(i);
  }
  return 0;
}

int ErasureCode::encode_chunks(const set<int> &want_to_encode,
                               map<int, bufferlist> *encoded)
{
  assert("ErasureCode::encode_chunks not implemented" == 0);
}
 
int ErasureCode::decode(const set<int> &want_to_read,
                        const map<int, bufferlist> &chunks,
                        map<int, bufferlist> *decoded)
{
  vector<int> have;
  have.reserve(chunks.size());
  for (map<int, bufferlist>::const_iterator i = chunks.begin();
       i != chunks.end();
       ++i) {
    have.push_back(i->first);
  }
  if (includes(
	have.begin(), have.end(), want_to_read.begin(), want_to_read.end())) {
    for (set<int>::iterator i = want_to_read.begin();
	 i != want_to_read.end();
	 ++i) {
      (*decoded)[*i] = chunks.find(*i)->second;
    }
    return 0;
  }
  unsigned int k = get_data_chunk_count();
  unsigned int m = get_chunk_count() - k;
  unsigned blocksize = (*chunks.begin()).second.length();
  for (unsigned int i =  0; i < k + m; i++) {
    if (chunks.find(i) == chunks.end()) {
      bufferptr ptr(buffer::create_page_aligned(blocksize));
      (*decoded)[i].push_front(ptr);
    } else {
      (*decoded)[i] = chunks.find(i)->second;
      (*decoded)[i].rebuild_page_aligned();
    }
  }
  return decode_chunks(want_to_read, chunks, decoded);
}

int ErasureCode::decode_chunks(const set<int> &want_to_read,
                               const map<int, bufferlist> &chunks,
                               map<int, bufferlist> *decoded)
{
  assert("ErasureCode::decode_chunks not implemented" == 0);
}

int ErasureCode::parse(const map<std::string,std::string> &parameters,
		       ostream *ss)
{
  return to_mapping(parameters, ss);
}

const vector<int> &ErasureCode::get_chunk_mapping() const {
  return chunk_mapping;
}

int ErasureCode::to_mapping(const map<std::string,std::string> &parameters,
			    ostream *ss)
{
  if (parameters.find("mapping") != parameters.end()) {
    std::string mapping = parameters.find("mapping")->second;
    int position = 0;
    vector<int> coding_chunk_mapping;
    for(std::string::iterator it = mapping.begin(); it != mapping.end(); ++it) {
      if (*it == 'D')
	chunk_mapping.push_back(position);
      else
	coding_chunk_mapping.push_back(position);
      position++;
    }
    chunk_mapping.insert(chunk_mapping.end(),
			 coding_chunk_mapping.begin(),
			 coding_chunk_mapping.end());
  }
  return 0;
}

int ErasureCode::to_int(const std::string &name,
			const map<std::string,std::string> &parameters,
			int *value,
			int default_value,
			ostream *ss)
{
  if (parameters.find(name) == parameters.end() ||
      parameters.find(name)->second.size() == 0) {
    *value = default_value;
    return 0;
  }
  std::string p = parameters.find(name)->second;
  std::string err;
  int r = strict_strtol(p.c_str(), 10, &err);
  if (!err.empty()) {
    *ss << "could not convert " << name << "=" << p
	<< " to int because " << err
	<< ", set to default " << default_value << std::endl;
    *value = default_value;
    return -EINVAL;
  }
  *value = r;
  return 0;
}

int ErasureCode::to_bool(const std::string &name,
			 const map<std::string,std::string> &parameters,
			 bool *value,
			 bool default_value,
			 ostream *ss)
{
  if (parameters.find(name) == parameters.end() ||
      parameters.find(name)->second.size() == 0) {
    *value = default_value;
    return 0;
  }
  const std::string p = parameters.find(name)->second;
  *value = (p == "yes") || (p == "true");
  return 0;
}

int ErasureCode::decode_concat(const map<int, bufferlist> &chunks,
			       bufferlist *decoded)
{
  set<int> want_to_read;

  for (unsigned int i = 0; i < get_data_chunk_count(); i++) {
    int chunk = chunk_mapping.size() > i ? chunk_mapping[i] : i;
    want_to_read.insert(chunk);
  }
  map<int, bufferlist> decoded_map;
  int r = decode(want_to_read, chunks, &decoded_map);
  if (r == 0) {
    for (unsigned int i = 0; i < get_data_chunk_count(); i++) {
      int chunk = chunk_mapping.size() > i ? chunk_mapping[i] : i;
      decoded->claim_append(decoded_map[chunk]);
    }
  }
  return r;
}
