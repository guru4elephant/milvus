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

#include "server/delivery/request/ShowTableInfoRequest.h"
#include "server/DBWrapper.h"
#include "utils/Log.h"
#include "utils/TimeRecorder.h"
#include "utils/ValidationUtil.h"

#include <memory>
#include <vector>

namespace milvus {
namespace server {

void
ConstructTableStat(const engine::TableStat& table_stat, TableStat& req_table_stat) {
    req_table_stat.table_name_ = table_stat.name_;
    int64_t row_count = 0;
    for (auto& seg : table_stat.segments_stat_) {
        SegmentStat seg_stat;
        seg_stat.name_ = seg.name_;
        seg_stat.row_num_ = seg.row_count_;
        seg_stat.index_name_ = seg.index_name_;
        seg_stat.data_size_ = seg.data_size_;
        req_table_stat.segments_stat_.emplace_back(seg_stat);
        row_count += seg.row_count_;
    }
    req_table_stat.total_row_num_ = row_count;
}

ShowTableInfoRequest::ShowTableInfoRequest(const std::shared_ptr<Context>& context, const std::string& table_name,
                                           TableInfo& table_info)
    : BaseRequest(context, INFO_REQUEST_GROUP), table_name_(table_name), table_info_(table_info) {
}

BaseRequestPtr
ShowTableInfoRequest::Create(const std::shared_ptr<Context>& context, const std::string& table_name,
                             TableInfo& table_info) {
    return std::shared_ptr<BaseRequest>(new ShowTableInfoRequest(context, table_name, table_info));
}

Status
ShowTableInfoRequest::OnExecute() {
    std::string hdr = "ShowTableInfoRequest(table=" + table_name_ + ")";
    TimeRecorderAuto rc(hdr);

    auto status = ValidationUtil::ValidateTableName(table_name_);
    if (!status.ok()) {
        return status;
    }

    bool exists = false;
    status = DBWrapper::DB()->HasTable(table_name_, exists);
    if (!status.ok()) {
        return status;
    }

    if (!exists) {
        return Status(SERVER_TABLE_NOT_EXIST, "Table " + table_name_ + " not exists");
    }

    engine::TableInfo table_info;
    status = DBWrapper::DB()->GetTableInfo(table_name_, table_info);
    if (!status.ok()) {
        return status;
    }

    int64_t total_row_count = 0;
    // contruct native info
    {
        ConstructTableStat(table_info.native_stat_, table_info_.native_stat_);
        total_row_count += table_info_.native_stat_.total_row_num_;
    }

    // construct partitions info
    table_info_.partitions_stat_.reserve(table_info.partitions_stat_.size());
    for (auto& partition : table_info.partitions_stat_) {
        TableStat table_stat;
        ConstructTableStat(partition, table_stat);
        total_row_count += table_stat.total_row_num_;
        table_info_.partitions_stat_.emplace_back(table_stat);
    }

    table_info_.total_row_num_ = total_row_count;

    return Status::OK();
}

}  // namespace server
}  // namespace milvus