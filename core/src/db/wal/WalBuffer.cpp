// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <cstring>
#include "WalBuffer.h"
#include "WalDefinations.h"

namespace milvus {
namespace engine {
namespace wal {

MXLogBuffer::MXLogBuffer(const std::string &mxlog_path,
const uint32_t &buffer_size)
: mxlog_buffer_size_(buffer_size)
, mxlog_writer_(mxlog_path)
{
    __glibcxx_assert(mxlog_buffer_size_ >= 0);
    mxlog_buffer_size_ = std::max(mxlog_buffer_size_, (uint32_t)WAL_BUFFER_MIN_SIZE * 1024 * 1024);
    if (Init()) {
        //todo: init fail, print error log
        return;
    }
}

MXLogBuffer::~MXLogBuffer() {
    /*
    if (buf_[0]){
        free(buf_[0]);
        buf_[0] = 0;
    }
    if (buf_[1]) {
        free(buf_[1]);
        buf_[1] = 0;
    }
     */
}

/**
 * alloc space for buffers
 * @param buffer_size
 * @return
 */
bool MXLogBuffer::Init() {
    //1:alloc space 4 two buffers
    //todo: use smart pointer
    /*
    buf_[0] = (char*)malloc(buffer_size);
    if (!buf_[0]) {
        return false;
    }
    buf_[1] = (char*)malloc(buffer_size);
    if (!buf_[1]) {
        if (buf_[0]) {
            free(buf_[0]);
            buf_[0] = 0;
        }
        return false;
    }
     */
    buf_[0] = BufferPtr(new char[mxlog_buffer_size_]);
    buf_[1] = BufferPtr(new char[mxlog_buffer_size_]);
    //2:init handlers of two buffers
    ReSet();
//    mxlog_buffer_writer_.lsn = mxlog_buffer_writer_.min_lsn = ${WalManager.current_lsn};
//    mxlog_buffer_reader_.lsn = mxlog_buffer_reader_.min_lsn = ${WalManager.current_lsn};

    return true;
}

void
MXLogBuffer::ReSet() {
    mxlog_buffer_writer_.buf_idx = mxlog_buffer_reader_.buf_idx = 0;
    mxlog_buffer_writer_.buf_offset = mxlog_buffer_reader_.buf_offset = 0;
    mxlog_buffer_reader_.file_no = 0;//reader file number equals 0 means read from buffer
    mxlog_buffer_reader_.max_offset = 0;// equals file size, 0 means read from buffer
}

//buffer writer cares about surplus space of buffer
uint64_t MXLogBuffer::SurplusSpace() {
    return mxlog_buffer_size_ - mxlog_buffer_writer_.buf_offset;
}

uint64_t MXLogBuffer::RecordSize(const size_t n,
                                 const size_t dim,
                                 const size_t table_id_size) {
    uint64_t data_size = 0;
    data_size += n * (sizeof(IDNumber) + sizeof(float) * dim);
    data_size += table_id_size;
    return data_size + (uint64_t)SizeOfMXLogRecordHeader;
}

bool MXLogBuffer::Append(const std::string &table_id,
                         const MXLogType& record_type,
                         const size_t& n,
                         const size_t& dim,
                         const float *vectors,
                         const milvus::engine::IDNumbers& vector_ids,
                         const size_t& vector_ids_offset,
                         bool update_file_no,
                         MXLogMetaHandler& meta_handler,
                         uint64_t& lsn) {

    uint64_t record_size = RecordSize(n, dim, table_id.size());
    if (SurplusSpace() < record_size) {
        if (mxlog_buffer_writer_.buf_idx != mxlog_buffer_reader_.buf_idx) {//no need to switch buffer
            mxlog_buffer_writer_.buf_offset = 0;
            //todo:important! get atomic increase file no from WalManager and update mxlog_buffer_wrter_.file_no
        } else { // swith writer buffer
            mxlog_buffer_writer_.buf_idx ^= 1;
            mxlog_buffer_writer_.buf_offset = 0;
            mxlog_buffer_reader_.max_offset = mxlog_buffer_writer_.max_offset;
            mxlog_buffer_writer_.max_offset = 0;
        }
        mxlog_buffer_writer_.file_no ++;
        mxlog_writer_.ReBorn(mxlog_buffer_writer_.file_no);
    }
    lsn = mxlog_buffer_writer_.file_no;
    lsn <<= 32;
    lsn += mxlog_buffer_writer_.buf_offset;//point to the offset of current record in wal file
    char* current_write_buf = buf_[mxlog_buffer_writer_.buf_idx].get();
    uint64_t current_write_offset = mxlog_buffer_writer_.buf_offset;
    memcpy(current_write_buf + current_write_offset, (char*)&record_size, 4);
    current_write_offset += 4;
    memcpy(current_write_buf + current_write_offset, (char*)&lsn, 8);
    current_write_offset += 8;
    memcpy(current_write_buf + current_write_offset, (char*)&n, 4);
    current_write_offset += 4;
    auto table_id_size = (uint16_t)table_id.size();
    memcpy(current_write_buf + current_write_offset, (char*)&table_id_size, 2);
    current_write_offset += 2;
    memcpy(current_write_buf + current_write_offset, (char*)&dim, 2);
    current_write_offset += 2;
    auto op_type = (uint8_t)MXLogType::Insert;
    memcpy(current_write_buf + current_write_offset, (char*)&op_type, 1);
    current_write_offset ++;
    memcpy(current_write_buf + current_write_offset, table_id.data(), table_id.size());
    current_write_offset += table_id.size();
    for (auto i = vector_ids_offset; i < vector_ids.size(); ++ i) {
        memcpy(current_write_buf + current_write_offset, (char*)&vector_ids[i], 8);
        current_write_offset += 8;
    }
    memcpy(current_write_buf + current_write_offset, vectors, (n * dim) << 2);
    current_write_offset += (n * dim) << 2;
    mxlog_buffer_writer_.buf_offset = (uint32_t)current_write_offset;
    mxlog_buffer_writer_.lsn = lsn;
    mxlog_writer_.Write(buf_[mxlog_buffer_writer_.buf_idx].get(), record_size);//default async flush
    if (update_file_no) {// transaction integrity
        meta_handler.SetMXLogInternalMeta(lsn, mxlog_buffer_writer_.file_no);
    }
    if (reader_is_waiting) {
        reader_cv.notify_one();
    }
    mxlog_buffer_writer_.max_offset = (uint32_t)current_write_offset;
    return true;
}

/**
 * wal thread invoke this interface get record from writer buffer or load from
 * wal log, then invoke memory table's interface
 * @param table_id
 * @param n
 * @param dim
 * @param vectors
 * @param vector_ids
 * @param lsn
 * @return
 */
bool MXLogBuffer::Next(std::string &table_id,
                       MXLogType& mxl_type,
                       size_t &n,
                       size_t &dim,
                       float *vectors,
                       milvus::engine::IDNumbers &vector_ids,
                       const uint64_t& last_applied_lsn,
                       uint64_t &lsn) {

    //reader catch up to writer, no next record, read fail
    if (mxlog_buffer_reader_.buf_idx == mxlog_buffer_writer_.buf_idx
      && mxlog_buffer_reader_.lsn == last_applied_lsn) {
        return false;
    }
    //otherwise, it means there must exists next record, in buffer or wal log
    char* current_read_buf = buf_[mxlog_buffer_reader_.buf_idx].get();
    uint64_t current_read_offset = mxlog_buffer_reader_.buf_offset;
    uint32_t record_size = 0;
    memcpy(&record_size, current_read_buf + current_read_offset, 4);
    current_read_offset += 4;
    memcpy(&lsn, current_read_buf + current_read_offset, 8);
    current_read_offset += 8;
    memcpy(&n, current_read_buf + current_read_offset, 4);
    current_read_offset += 4;
    uint16_t table_id_size, d;
    memcpy(&table_id_size, current_read_buf + current_read_offset, 2);
    current_read_offset += 2;
    memcpy(&d, current_read_buf + current_read_offset, 2);
    dim = d;
    current_read_offset += 2;
    memcpy(&mxl_type, current_read_buf + current_read_offset, 1);
    current_read_offset += 1;
    table_id.resize(table_id_size);
    for (auto i = 0; i < table_id_size; ++ i) {
        table_id[i] = *(current_read_buf + current_read_offset);
        ++ current_read_offset;
    }
    int64_t tmp_id;
    for (auto i = 0; i < n; ++ i) {
        memcpy(&tmp_id, current_read_buf + current_read_offset, 8);
        vector_ids.emplace_back((int64_t)tmp_id);
        current_read_offset += 8;
    }
    vectors = (float*)malloc((n * dim) << 2);
    __glibcxx_assert(vectors != NULL);
    memcpy(vectors, current_read_buf + current_read_offset, (n * dim) << 2);
    current_read_offset += (n * dim) << 2;
    mxlog_buffer_reader_.lsn = lsn;// last consumed record
    if ((uint32_t)(lsn & LSN_OFFSET_MASK) + record_size == mxlog_buffer_reader_.max_offset) { // last record
        if ((uint32_t)(lsn >> 32) + 1 == (uint32_t)(last_applied_lsn >> 32)) {
            //todo: add lock to forbidden buffer_writer switch buffer
            mxlog_buffer_reader_.buf_idx ^= 1;
            mxlog_buffer_reader_.buf_offset = 0;
            mxlog_buffer_reader_.file_no = mxlog_buffer_writer_.file_no;
            mxlog_buffer_reader_.max_offset = mxlog_buffer_writer_.max_offset;
        } else {
            //todo: load wal log from disk
            MXLogFileHandler mxlog_reader(mxlog_writer_.GetFilePath());
            mxlog_reader.SetFileName(std::to_string(mxlog_buffer_reader_.file_no + 1) + ".wal");
            mxlog_reader.SetFileOpenMode("r");
            mxlog_reader.OpenFile();
            if (mxlog_reader.IsOpen()) {
                mxlog_reader.Load(buf_[mxlog_buffer_reader_.buf_idx].get());
                mxlog_buffer_reader_.max_offset = (uint32_t)mxlog_reader.GetFileSize();
                mxlog_buffer_reader_.buf_offset = 0;
                mxlog_buffer_reader_.file_no ++;
            } else {
                //todo: log error: wal log open fail
            }
        }
    }
    return true;
}

bool
MXLogBuffer::Next() {

}

bool Delete(const std::string& table_id, const milvus::engine::IDNumbers& vector_ids) {

}

void
MXLogBuffer::Flush(const std::string &table_id) {
}

bool
MXLogBuffer::LoadForRecovery(const uint64_t &lsn) {
    if ((uint32_t)(lsn >> 32) == mxlog_buffer_reader_.file_no) {
        return true;
    }
    mxlog_writer_.SetFileName(std::to_string(lsn>>32) + ".wal");
    mxlog_writer_.SetFileOpenMode("r");
    if (!mxlog_writer_.FileExists()) {
        return false;
    }
    mxlog_writer_.Load(buf_[mxlog_buffer_reader_.buf_idx].get());
    mxlog_buffer_reader_.buf_offset = (uint32_t)(lsn & LSN_OFFSET_MASK);
    mxlog_buffer_reader_.file_no = (uint32_t)(lsn >> 32);
}

bool
MXLogBuffer::NextInfo(std::string &table_id, uint64_t &next_lsn) {
    if (mxlog_buffer_reader_.buf_offset == mxlog_writer_.GetFileSize()) {
        mxlog_writer_.CloseFile();
        mxlog_writer_.DeleteFile();
        mxlog_buffer_reader_.file_no ++;
        next_lsn = mxlog_buffer_reader_.file_no;
        next_lsn <<= 32;
        if (LoadForRecovery(next_lsn)) {
            return false;
        }
        uint16_t table_id_len;
        char *p_buf = buf_[mxlog_buffer_reader_.buf_idx].get();
        memcpy((char*)&table_id_len, p_buf + mxlog_buffer_reader_.buf_offset + offsetof(MXLogRecord, vector_num), 2);
        table_id.resize((size_t)table_id_len);
        for (auto idx = mxlog_buffer_reader_.buf_offset + (uint32_t)offsetof(MXLogRecord, mxl_type); idx < table_id_len; ++ idx)
            table_id += *(p_buf + idx);
    } else {
        uint16_t table_id_len;
        char *p_buf = buf_[mxlog_buffer_reader_.buf_idx].get();
        memcpy((char*)&table_id_len, p_buf + mxlog_buffer_reader_.buf_offset + offsetof(MXLogRecord, vector_num), 2);
        table_id.resize((size_t)table_id_len);
        for (auto idx = mxlog_buffer_reader_.buf_offset + (uint32_t)offsetof(MXLogRecord, mxl_type); idx < table_id_len; ++ idx)
            table_id += *(p_buf + idx);
        uint32_t current_record_len;
        memcpy((char*)&current_record_len, p_buf + mxlog_buffer_reader_.buf_offset, 4);
        return true;
    }
}

uint32_t
MXLogBuffer::GetWriterFileNo() {
    return mxlog_buffer_writer_.file_no;
}

void
MXLogBuffer::SetWriterFileNo(const uint32_t &file_no) {

}

} // wal
} // engine
} // milvus