//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once

#include <stdint.h>

#include <memory>

#include "db/blob_log_format.h"
#include "rocksdb/slice.h"
#include "rocksdb/status.h"
#include "rocksdb/types.h"

namespace rocksdb {

class WritableFileWriter;

using std::unique_ptr;

namespace blob_log {

/**
 * Writer is a general purpose log stream writer. It provides an append-only
 * abstraction for writing data. The details of the how the data is written is
 * handled by the WriteableFile sub-class implementation.
 *
 * File format:
 *
 * File is broken down into variable sized records. The format of each record
 * is described below.
 *       +-----+-------------+--+----+----------+------+-- ... ----+
 * File  | r0  |        r1   |P | r2 |    r3    |  r4  |           |
 *       +-----+-------------+--+----+----------+------+-- ... ----+
 *       <--- kBlockSize ------>|<-- kBlockSize ------>|
 *  rn = variable size records
 *  P = Padding
 *
 * Data is written out in kBlockSize chunks. If next record does not fit
 * into the space left, the leftover space will be padded with \0.
 *
 * Legacy record format:
 *
 * +---------+-----------+-----------+--- ... ---+
 * |CRC (4B) | Size (2B) | Type (1B) | Payload   |
 * +---------+-----------+-----------+--- ... ---+
 *
 * CRC = 32bit hash computed over the payload using CRC
 * Size = Length of the payload data
 * Type = Type of record
 *        (kZeroType, kFullType, kFirstType, kLastType, kMiddleType )
 *        The type is used to group a bunch of records together to represent
 *        blocks that are larger than kBlockSize
 * Payload = Byte stream as long as specified by the payload size
 *
 * Recyclable record format:
 *
 * +---------+-----------+-----------+----------------+--- ... ---+
 * |CRC (4B) | Size (2B) | Type (1B) | Log number (4B)| Payload   |
 * +---------+-----------+-----------+----------------+--- ... ---+
 *
 * Same as above, with the addition of
 * Log number = 32bit log file number, so that we can distinguish between
 * records written by the most recent log writer vs a previous one.
 */
class Writer {
 public:
  // Create a writer that will append data to "*dest".
  // "*dest" must be initially empty.
  // "*dest" must remain live while this Writer is in use.
  explicit Writer(unique_ptr<WritableFileWriter>&& dest,
                  uint64_t log_number, uint64_t bpsync,
                  bool use_fsync, uint64_t boffset = 0);
  ~Writer();

  static void ConstructBlobHeader(char *headerbuf, const Slice& key,
    const Slice& val, int32_t ttl, int64_t ts);

  Status AddRecord(const Slice& key, const Slice& val,
    uint64_t& key_offset, uint64_t& blob_offset);

  Status AddRecord(const Slice& key, const Slice& val,
    uint64_t& key_offset, uint64_t& blob_offset, uint32_t ttl);

  Status EmitPhysicalRecord(const char *headerbuf, const Slice& key,
    const Slice& val, uint64_t& key_offset, uint64_t& blob_offset);

  Status AddRecordFooter(const SequenceNumber& sn);

  Status AppendFooter(blob_log::BlobLogFooter& footer);

  Status WriteHeader(blob_log::BlobLogHeader& header);

  WritableFileWriter* file() { return dest_.get(); }

  const WritableFileWriter* file() const { return dest_.get(); }

  uint64_t get_log_number() const { return log_number_; }

  bool ShouldSync() const { return block_offset_ > next_sync_offset_; }

  void Sync();

  void ResetSyncPointer() { next_sync_offset_ += bytes_per_sync_; }

 private:
  unique_ptr<WritableFileWriter> dest_;
  uint64_t log_number_;
  uint64_t block_offset_;       // Current offset in block
  uint64_t bytes_per_sync_;
  uint64_t next_sync_offset_;
  bool use_fsync_;

  // crc32c values for all supported record types.  These are
  // pre-computed to reduce the overhead of computing the crc of the
  // record type stored in the header.
  uint32_t type_crc_[kMaxRecordType + 1];

  // No copying allowed
  Writer(const Writer&);
  void operator=(const Writer&);
};

}  // namespace blob_log
}  // namespace rocksdb
