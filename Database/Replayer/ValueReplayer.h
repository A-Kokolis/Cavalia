#pragma once
#ifndef __CAVALIA_DATABASE_VALUE_REPLAYER_H__
#define __CAVALIA_DATABASE_VALUE_REPLAYER_H__

#include "../Storage/BaseStorageManager.h"
#include "BaseReplayer.h"

namespace Cavalia{
	namespace Database{

		struct AccessLog{
			uint8_t type_;
			size_t table_id_;
			size_t data_size_;
			char *data_ptr_;
		};

		struct TxnLog{
			TxnLog(){
				log_count_ = 0;
				commit_ts_ = 0;
			}

			AccessLog* NewAccessLog(){
				assert(log_count_ < kMaxAccessNum);
				AccessLog *ret = &logs_[log_count_];
				++log_count_;
				return ret;
			}

			void Clear(){
				log_count_ = 0;
			}

			AccessLog logs_[kMaxAccessNum];
			size_t log_count_;
			uint64_t commit_ts_;
		};

		class ValueReplayer : public BaseReplayer{
		public:
			ValueReplayer(const std::string &filename, BaseStorageManager *const storage_manager, const size_t &thread_count) : BaseReplayer(filename, storage_manager, thread_count, true){}
			virtual ~ValueReplayer(){}

			virtual void Start(){
				boost::thread_group reloaders;
				for (size_t i = 0; i < thread_count_; ++i){
					reloaders.create_thread(boost::bind(&ValueReplayer::ReloadLog, this, i));
				}
				reloaders.join_all();
			}

			void ReloadLog(const size_t &thread_id){
				FILE *infile_ptr = infiles_[thread_id];
				fseek(infile_ptr, 0L, SEEK_END);
				size_t file_size = ftell(infile_ptr);
				rewind(infile_ptr);
				size_t file_pos = 0;
				int result = 0;
				while (file_pos < file_size){
					TxnLog *txn_log = new TxnLog();
					size_t txn_size;
					result = fread(&txn_size, sizeof(txn_size), 1, infile_ptr);
					assert(result == 1);
					uint64_t commit_ts;
					result = fread(&commit_ts, sizeof(commit_ts), 1, infile_ptr);
					assert(result == 1);
					// set commit timestamp.
					txn_log->commit_ts_ = commit_ts;
					size_t txn_pos = sizeof(txn_size) + sizeof(commit_ts);
					while (txn_pos < txn_size){
						AccessLog *access_log = txn_log->NewAccessLog();
						result = fread(&access_log->type_, sizeof(access_log->type_), 1, infile_ptr);
						assert(result == 1);
						txn_pos += sizeof(access_log->type_);
						result = fread(&access_log->table_id_, sizeof(access_log->table_id_), 1, infile_ptr);
						assert(result == 1);
						txn_pos += sizeof(access_log->table_id_);
						result = fread(&access_log->data_size_, sizeof(access_log->data_size_), 1, infile_ptr);
						assert(result == 1);
						txn_pos += sizeof(access_log->data_size_);
						access_log->data_ptr_ = new char[access_log->data_size_];
						result = fread(access_log->data_ptr_, 1, access_log->data_size_, infile_ptr);
						assert(result == access_log->data_size_);
						txn_pos += access_log->data_size_;
					}
					assert(txn_pos == txn_size);
					file_pos += txn_size;
					txn_logs_.push_back(txn_log);
				}
				assert(file_pos == file_size);
			}

			void ProcessLog(const size_t &thread_id){
				for (size_t i = 0; i < txn_logs_.size(); ++i){
					
				}
			}

		private:
			ValueReplayer(const ValueReplayer &);
			ValueReplayer& operator=(const ValueReplayer &);

		private:
			std::vector<TxnLog*> txn_logs_;
		};
	}
}

#endif
