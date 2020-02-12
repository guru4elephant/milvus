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

#include "db/wal/WalDefinations.h"
#define private public
#include <gtest/gtest.h>
#include <stdlib.h>
#include <time.h>

#include <fstream>
#include <sstream>
#include <thread>

#include "db/wal/WalBuffer.h"
#include "db/wal/WalFileHandler.h"
#include "db/wal/WalManager.h"
#include "db/wal/WalMetaHandler.h"
#include "utils/Error.h"

#define WAL_GTEST_PATH "/tmp/milvus/wal/test/"  // end with '/'

void
MakeEmptyTestPath() {
    if (access(WAL_GTEST_PATH, 0) == 0) {
        ::system("rm -rf " WAL_GTEST_PATH "*");
    } else {
        ::system("mkdir -m 777 -p " WAL_GTEST_PATH);
    }
}

TEST(WalTest, FILE_HANDLER_TEST) {
    MakeEmptyTestPath();

    std::string file_name = "1.wal";
    milvus::engine::wal::MXLogFileHandler file_handler(WAL_GTEST_PATH);
    file_handler.SetFileName(file_name);
    file_handler.SetFileOpenMode("w");
    ASSERT_FALSE(file_handler.FileExists());
    ASSERT_FALSE(file_handler.IsOpen());

    ASSERT_TRUE(file_handler.OpenFile());
    ASSERT_TRUE(file_handler.IsOpen());
    ASSERT_EQ(0, file_handler.GetFileSize());

    std::string write_content = "hello, world!\n";
    ASSERT_TRUE(file_handler.Write(const_cast<char*>(write_content.data()), write_content.size()));
    ASSERT_TRUE(file_handler.CloseFile());

    file_handler.SetFileOpenMode("r");
    char* buf = (char*)malloc(write_content.size() + 10);
    memset(buf, 0, write_content.size() + 10);
    ASSERT_TRUE(file_handler.Load(buf, 0, write_content.size()));
    ASSERT_STREQ(buf, write_content.c_str());
    free(buf);
    ASSERT_TRUE(file_handler.CloseFile());
    file_handler.DeleteFile();

    file_handler.SetFileOpenMode("w");
    file_handler.ReBorn("2");
    write_content += ", aaaaa";
    file_handler.Write(const_cast<char*>(write_content.data()), write_content.size());
    ASSERT_EQ("2", file_handler.GetFileName());
    ASSERT_TRUE(file_handler.CloseFile());
    file_handler.DeleteFile();
}

TEST(WalTest, META_HANDLER_TEST) {
    MakeEmptyTestPath();

    milvus::engine::wal::MXLogMetaHandler meta_handler(WAL_GTEST_PATH);
    uint64_t wal_lsn = 103920;
    ASSERT_TRUE(meta_handler.SetMXLogInternalMeta(wal_lsn));
    uint64_t internal_lsn;
    ASSERT_TRUE(meta_handler.GetMXLogInternalMeta(internal_lsn));
    ASSERT_EQ(wal_lsn, internal_lsn);
}

TEST(WalTest, BUFFER_INIT_TEST) {
    MakeEmptyTestPath();

    FILE* fi = nullptr;
    char buff[128];
    milvus::engine::wal::MXLogBuffer buffer(WAL_GTEST_PATH, 0);

    // start_lsn == end_lsn, start_lsn == 0
    ASSERT_TRUE(buffer.Init(0, 0));
    ASSERT_EQ(buffer.mxlog_buffer_reader_.file_no, 0);
    ASSERT_EQ(buffer.mxlog_buffer_reader_.buf_offset, 0);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.file_no, 0);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.buf_offset, 0);
    ASSERT_EQ(buffer.file_no_from_, 0);

    // start_lsn == end_lsn, start_lsn != 0
    uint32_t file_no = 1;
    uint32_t buf_off = 32;
    uint64_t lsn = (uint64_t)file_no << 32 | buf_off;
    ASSERT_TRUE(buffer.Init(lsn, lsn));
    ASSERT_EQ(buffer.mxlog_buffer_reader_.file_no, file_no + 1);
    ASSERT_EQ(buffer.mxlog_buffer_reader_.buf_offset, 0);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.file_no, file_no + 1);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.buf_offset, 0);
    ASSERT_EQ(buffer.file_no_from_, file_no + 1);

    // start_lsn != end_lsn, start_file == end_file
    uint32_t start_file_no = 3;
    uint32_t start_buf_off = 32;
    uint64_t start_lsn = (uint64_t)start_file_no << 32 | start_buf_off;
    uint32_t end_file_no = 3;
    uint32_t end_buf_off = 64;
    uint64_t end_lsn = (uint64_t)end_file_no << 32 | end_buf_off;
    ASSERT_FALSE(buffer.Init(start_lsn, end_lsn));  // file not exist
    fi = fopen(WAL_GTEST_PATH "3.wal", "w");
    fclose(fi);
    ASSERT_FALSE(buffer.Init(start_lsn, end_lsn));  // file size zero
    fi = fopen(WAL_GTEST_PATH "3.wal", "w");
    fwrite(buff, 1, end_buf_off - 1, fi);
    fclose(fi);
    ASSERT_FALSE(buffer.Init(start_lsn, end_lsn));  // file size error
    fi = fopen(WAL_GTEST_PATH "3.wal", "w");
    fwrite(buff, 1, end_buf_off, fi);
    fclose(fi);
    ASSERT_TRUE(buffer.Init(start_lsn, end_lsn));  // success
    ASSERT_EQ(buffer.mxlog_buffer_reader_.file_no, start_file_no);
    ASSERT_EQ(buffer.mxlog_buffer_reader_.buf_offset, start_buf_off);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.file_no, end_file_no);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.buf_offset, end_buf_off);
    ASSERT_EQ(buffer.file_no_from_, start_file_no);

    // start_lsn != end_lsn, start_file != end_file
    start_file_no = 4;
    start_buf_off = 32;
    start_lsn = (uint64_t)start_file_no << 32 | start_buf_off;
    end_file_no = 5;
    end_buf_off = 64;
    end_lsn = (uint64_t)end_file_no << 32 | end_buf_off;
    ASSERT_FALSE(buffer.Init(start_lsn, end_lsn));  // file 4 not exist
    fi = fopen(WAL_GTEST_PATH "4.wal", "w");
    fwrite(buff, 1, start_buf_off, fi);
    fclose(fi);
    ASSERT_FALSE(buffer.Init(start_lsn, end_lsn));  // file 5 not exist
    fi = fopen(WAL_GTEST_PATH "5.wal", "w");
    fclose(fi);
    ASSERT_FALSE(buffer.Init(start_lsn, end_lsn));  // file 5 size error
    fi = fopen(WAL_GTEST_PATH "5.wal", "w");
    fwrite(buff, 1, end_buf_off, fi);
    fclose(fi);
    buffer.mxlog_buffer_size_ = 0;                 // to correct the buff size by buffer_size_need
    ASSERT_TRUE(buffer.Init(start_lsn, end_lsn));  // success
    ASSERT_EQ(buffer.mxlog_buffer_reader_.file_no, start_file_no);
    ASSERT_EQ(buffer.mxlog_buffer_reader_.buf_offset, start_buf_off);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.file_no, end_file_no);
    ASSERT_EQ(buffer.mxlog_buffer_writer_.buf_offset, end_buf_off);
    ASSERT_EQ(buffer.file_no_from_, start_file_no);
    ASSERT_EQ(buffer.mxlog_buffer_size_, end_buf_off);
}

TEST(WalTest, BUFFER_TEST) {
    MakeEmptyTestPath();

    milvus::engine::wal::MXLogBuffer buffer(WAL_GTEST_PATH, WAL_BUFFER_MAX_SIZE + 1);

    uint32_t file_no = 4;
    uint32_t buf_off = 100;
    uint64_t lsn = (uint64_t)file_no << 32 | buf_off;
    buffer.mxlog_buffer_size_ = 1000;
    buffer.Reset(lsn);

    milvus::engine::wal::MXLogRecord record[4];
    milvus::engine::wal::MXLogRecord read_rst;

    // write 0
    record[0].type = milvus::engine::wal::MXLogType::InsertVector;
    record[0].table_id = "insert_table";
    record[0].partition_tag = "parti1";
    record[0].length = 50;
    record[0].ids = (milvus::engine::IDNumber*)malloc(record[0].length * sizeof(milvus::engine::IDNumber));
    record[0].data_size = record[0].length * sizeof(float);
    record[0].data = malloc(record[0].data_size);
    ASSERT_EQ(buffer.Append(record[0]), milvus::WAL_SUCCESS);
    uint32_t new_file_no = uint32_t(record[0].lsn >> 32);
    ASSERT_EQ(new_file_no, ++file_no);

    // write 1
    record[1].type = milvus::engine::wal::MXLogType::Delete;
    record[1].table_id = "insert_table";
    record[1].partition_tag = "parti1";
    record[1].length = 10;
    record[1].ids = (milvus::engine::IDNumber*)malloc(record[0].length * sizeof(milvus::engine::IDNumber));
    record[1].data_size = 0;
    record[1].data = nullptr;
    ASSERT_EQ(buffer.Append(record[1]), milvus::WAL_SUCCESS);
    new_file_no = uint32_t(record[1].lsn >> 32);
    ASSERT_EQ(new_file_no, file_no);

    // read 0
    ASSERT_EQ(buffer.Next(record[1].lsn, read_rst), milvus::WAL_SUCCESS);
    ASSERT_EQ(read_rst.type, record[0].type);
    ASSERT_EQ(read_rst.table_id, record[0].table_id);
    ASSERT_EQ(read_rst.partition_tag, record[0].partition_tag);
    ASSERT_EQ(read_rst.length, record[0].length);
    ASSERT_EQ(memcmp(read_rst.ids, record[0].ids, read_rst.length * sizeof(milvus::engine::IDNumber)), 0);
    ASSERT_EQ(read_rst.data_size, record[0].data_size);
    ASSERT_EQ(memcmp(read_rst.data, record[0].data, read_rst.data_size), 0);

    // read 1
    ASSERT_EQ(buffer.Next(record[1].lsn, read_rst), milvus::WAL_SUCCESS);
    ASSERT_EQ(read_rst.type, record[1].type);
    ASSERT_EQ(read_rst.table_id, record[1].table_id);
    ASSERT_EQ(read_rst.partition_tag, record[1].partition_tag);
    ASSERT_EQ(read_rst.length, record[1].length);
    ASSERT_EQ(memcmp(read_rst.ids, record[1].ids, read_rst.length * sizeof(milvus::engine::IDNumber)), 0);
    ASSERT_EQ(read_rst.data_size, 0);
    ASSERT_EQ(read_rst.data, nullptr);

    // read empty
    ASSERT_EQ(buffer.Next(record[1].lsn, read_rst), milvus::WAL_SUCCESS);
    ASSERT_EQ(read_rst.type, milvus::engine::wal::MXLogType::None);

    // write 2 (new file)
    record[2].type = milvus::engine::wal::MXLogType::InsertVector;
    record[2].table_id = "insert_table";
    record[2].partition_tag = "parti1";
    record[2].length = 50;
    record[2].ids = (milvus::engine::IDNumber*)malloc(record[2].length * sizeof(milvus::engine::IDNumber));
    record[2].data_size = record[2].length * sizeof(float);
    record[2].data = malloc(record[2].data_size);
    ASSERT_EQ(buffer.Append(record[2]), milvus::WAL_SUCCESS);
    new_file_no = uint32_t(record[2].lsn >> 32);
    ASSERT_EQ(new_file_no, ++file_no);

    // write 3 (new file)
    record[3].type = milvus::engine::wal::MXLogType::InsertBinary;
    record[3].table_id = "insert_table";
    record[3].partition_tag = "parti1";
    record[3].length = 100;
    record[3].ids = (milvus::engine::IDNumber*)malloc(record[3].length * sizeof(milvus::engine::IDNumber));
    record[3].data_size = record[3].length * sizeof(uint8_t);
    record[3].data = malloc(record[3].data_size);
    ASSERT_EQ(buffer.Append(record[3]), milvus::WAL_SUCCESS);
    new_file_no = uint32_t(record[3].lsn >> 32);
    ASSERT_EQ(new_file_no, ++file_no);

    // read 2
    ASSERT_EQ(buffer.Next(record[3].lsn, read_rst), milvus::WAL_SUCCESS);
    ASSERT_EQ(read_rst.type, record[2].type);
    ASSERT_EQ(read_rst.table_id, record[2].table_id);
    ASSERT_EQ(read_rst.partition_tag, record[2].partition_tag);
    ASSERT_EQ(read_rst.length, record[2].length);
    ASSERT_EQ(memcmp(read_rst.ids, record[2].ids, read_rst.length * sizeof(milvus::engine::IDNumber)), 0);
    ASSERT_EQ(read_rst.data_size, record[2].data_size);
    ASSERT_EQ(memcmp(read_rst.data, record[2].data, read_rst.data_size), 0);

    // read 3
    ASSERT_EQ(buffer.Next(record[3].lsn, read_rst), milvus::WAL_SUCCESS);
    ASSERT_EQ(read_rst.type, record[3].type);
    ASSERT_EQ(read_rst.table_id, record[3].table_id);
    ASSERT_EQ(read_rst.partition_tag, record[3].partition_tag);
    ASSERT_EQ(read_rst.length, record[3].length);
    ASSERT_EQ(memcmp(read_rst.ids, record[3].ids, read_rst.length * sizeof(milvus::engine::IDNumber)), 0);
    ASSERT_EQ(read_rst.data_size, record[3].data_size);
    ASSERT_EQ(memcmp(read_rst.data, record[3].data, read_rst.data_size), 0);

    for (int i = 0; i < 3; i++) {
        if (record[i].ids != nullptr) {
            free((void*)record[i].ids);
        }
        if (record[i].data != nullptr) {
            free((void*)record[i].data);
        }
    }
}

#if 0
TEST(WalTest, MANAGER_TEST) {
    milvus::engine::wal::MXLogConfiguration wal_config;
    wal_config.mxlog_path = "/tmp/milvus/wal/";
    wal_config.record_size = 2 * 1024 * 1024;
    wal_config.buffer_size = 32 * 1024 * 1024;
    wal_config.recovery_error_ignore = true;

    milvus::engine::wal::WalManager manager(wal_config);
    manager.Init(nullptr);
    std::string table_id = "manager_test";
    std::string partition_tag = "parti2";
    milvus::engine::IDNumbers vec_ids;
    std::vector<float> vecs;
    vec_ids.emplace_back(1);
    vec_ids.emplace_back(2);
    vecs.emplace_back(1.2);
    vecs.emplace_back(3.4);
    vecs.emplace_back(5.6);
    vecs.emplace_back(7.8);

    manager.CreateTable(table_id);
    auto ins_res = manager.Insert(table_id, partition_tag, vec_ids, vecs);
    ASSERT_TRUE(ins_res);
    milvus::engine::IDNumbers del_ids;
    del_ids.emplace_back(2);
    auto del_ret = manager.DeleteById(table_id, del_ids);
    ASSERT_TRUE(del_ret);
    manager.Flush(table_id);
}

TEST(WalTest, LargeScaleRecords) {
    std::string data_path = "/home/zilliz/workspace/data/";
    milvus::engine::wal::MXLogConfiguration wal_config;
    wal_config.mxlog_path = "/tmp/milvus/wal/";
    wal_config.record_size = 2 * 1024 * 1024;
    wal_config.buffer_size = 32 * 1024 * 1024;
    wal_config.recovery_error_ignore = true;

    milvus::engine::wal::WalManager manager1(wal_config);
    manager1.mxlog_config_.buffer_size = 32 * 1024 * 1024;
    manager1.Init(nullptr);
    std::ifstream fin(data_path + "1.dat", std::ios::in);
    std::vector<milvus::engine::IDNumber> ids;
    std::vector<float> vecs;
    std::vector<uint8_t> bins;
    int type = -1;
    std::string line;

    while (getline(fin, line)) {
        std::istringstream istr(line);
        int cur_type, cur_id;
        istr >> cur_type;
        if (cur_type != type) {
            switch (type) {
                case 0:
                    manager1.Flush();
                    break;
                case 1:
                    manager1.Insert("insert_vector", "parti1", ids, vecs);
                    break;
                case 2:
                    manager1.Insert("insert_binary", "parti2", ids, bins);
                    break;
                case 3:
                    manager1.DeleteById("insert_vector", ids);
                    break;
                default:
                    std::cout << "invalid type: " << type << std::endl;
                    break;
            }
            ids.clear();
            vecs.clear();
            bins.clear();
        }
        type = cur_type;
        istr >> cur_id;
        ids.emplace_back(cur_id);
        if (cur_type == 1) {
            float v;
            for (auto i = 0; i < 10; ++i) {
                istr >> v;
                vecs.emplace_back(v);
            }
        } else if (cur_type == 2) {
            uint8_t b;
            for (auto i = 0; i < 20; ++i) {
                istr >> b;
                bins.emplace_back(b);
            }
        }
    }
    switch (type) {
        case 0:
            manager1.Flush();
            break;
        case 1:
            manager1.Insert("insert_vector", "parti1", ids, vecs);
            break;
        case 2:
            manager1.Insert("insert_binary", "parti2", ids, bins);
            break;
        case 3:
            manager1.DeleteById("insert_vector", ids);
            break;
        default:
            std::cout << "invalid type: " << type << std::endl;
            break;
    }
    fin.close();
}

TEST(WalTest, MultiThreadTest) {
    std::string data_path = "/home/zilliz/workspace/data/";
    milvus::engine::wal::MXLogConfiguration wal_config;
    wal_config.mxlog_path = "/tmp/milvus/wal/";
    wal_config.record_size = 2 * 1024 * 1024;
    wal_config.buffer_size = 32 * 1024 * 1024;
    wal_config.recovery_error_ignore = true;
    milvus::engine::wal::WalManager manager(wal_config);
    manager.mxlog_config_.buffer_size = 32 * 1024 * 1024;
    manager.Init(nullptr);
    auto read_fun = [&]() {
        std::ifstream fin(data_path + "1.dat", std::ios::in);
        std::vector<milvus::engine::IDNumber> ids;
        std::vector<float> vecs;
        std::vector<uint8_t> bins;
        int type = -1;
        std::string line;

        while (getline(fin, line)) {
            std::istringstream istr(line);
            int cur_type, cur_id;
            istr >> cur_type;
            if (cur_type != type) {
                switch (type) {
                    case 0:
                        manager.Flush();
                        break;
                    case 1:
                        manager.Insert("insert_vector", "parti1", ids, vecs);
                        break;
                    case 2:
                        manager.Insert("insert_binary", "parti2", ids, bins);
                        break;
                    case 3:
                        manager.DeleteById("insert_vector", ids);
                        break;
                    default:
                        std::cout << "invalid type: " << type << std::endl;
                        break;
                }
                ids.clear();
                vecs.clear();
                bins.clear();
            }
            type = cur_type;
            istr >> cur_id;
            ids.emplace_back(cur_id);
            if (cur_type == 1) {
                float v;
                for (auto i = 0; i < 10; ++i) {
                    istr >> v;
                    vecs.emplace_back(v);
                }
            } else if (cur_type == 2) {
                uint8_t b;
                for (auto i = 0; i < 20; ++i) {
                    istr >> b;
                    bins.emplace_back(b);
                }
            }
        }
        switch (type) {
            case 0:
                manager.Flush();
                break;
            case 1:
                manager.Insert("insert_vector", "parti1", ids, vecs);
                break;
            case 2:
                manager.Insert("insert_binary", "parti2", ids, bins);
                break;
            case 3:
                manager.DeleteById("insert_vector", ids);
                break;
            default:
                std::cout << "invalid type: " << type << std::endl;
                break;
        }
        fin.close();
    };

    auto write_fun = [&]() {
    };
    std::thread read_thread(read_fun);
    std::thread write_thread(write_fun);
    read_thread.join();
    write_thread.join();
}
#endif
