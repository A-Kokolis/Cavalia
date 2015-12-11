#pragma once
#ifndef __CAVALIA_DATABASE_ACCESS_LOGGER_H__
#define __CAVALIA_DATABASE_ACCESS_LOGGER_H__

#include "../Meta/MetaConstants.h"
#include "BaseLogger.h"
#if defined(COMPRESSION)
#include <lz4frame.h>
#endif
namespace Cavalia{
	namespace Database{
		class AccessLogger : public BaseLogger{
		public:
			static const int LZ4_HEADER_SIZE = 19;
			static const int LZ4_FOOTER_SIZE = 4;
			AccessLogger(const std::string &dir_name, const size_t &thread_count) : BaseLogger(dir_name, thread_count, true){
				buffers_ = new char*[thread_count_];
				buffer_offsets_ = new size_t[thread_count_];
				txn_offsets_ = new size_t[thread_count_];
				for (size_t i = 0; i < thread_count_; ++i){
					buffers_[i] = new char[kValueLogBufferSize];
					buffer_offsets_[i] = 0;
					txn_offsets_[i] = sizeof(size_t);
				}
				last_epochs_ = new uint64_t[thread_count_];
				for (size_t i = 0; i < thread_count_; ++i){
					last_epochs_[i] = 0;
				}
#if defined(COMPRESSION)
				compression_contexts_ = new LZ4F_compressionContext_t[thread_count_];
				for (size_t i = 0; i < thread_count_; ++i){
					LZ4F_errorCode_t err = LZ4F_createCompressionContext(&compression_contexts_[i], LZ4F_VERSION);
					assert(LZ4F_isError(err) == false);
				}
				size_t frame_size = LZ4F_compressBound(kValueLogBufferSize, NULL);
				// compressed_buf_size_ is the max size of file write
				compressed_buf_size_ = frame_size + LZ4_HEADER_SIZE + LZ4_FOOTER_SIZE;

				compressed_buffers_ = new char*[thread_count_];
				compressed_buf_offsets_ = new size_t[thread_count_];
				for (size_t i = 0; i < thread_count_; ++i){
					compressed_buf_offsets_[i] = 0;
					compressed_buffers_[i] = new char[compressed_buf_size_];
				}

				// compress begin: put header
				for (size_t i = 0; i < thread_count_; ++i){
					compressed_buf_offsets_[i] = LZ4F_compressBegin(compression_contexts_[i], compressed_buffers_[i], compressed_buf_size_, NULL);
				}
#endif
			}
			virtual ~AccessLogger(){
#if defined(COMPRESSION)
				for (size_t i = 0; i < thread_count_; ++i){
					size_t& offset = compressed_buf_offsets_[i];
					size_t n = LZ4F_compressEnd(compression_contexts_[i], compressed_buffers_[i] + offset, compressed_buf_size_ - offset, NULL);
					assert(LZ4F_isError(n) == false);
					offset += n;

					FILE *file_ptr = outfiles_[i];
					fwrite(compressed_buffers_[i], sizeof(char), offset, file_ptr);

					LZ4F_freeCompressionContext(compression_contexts_[i]);
				}
				delete[] compression_contexts_;
				compression_contexts_ = NULL;
				for (size_t i = 0; i < thread_count_; ++i){
					delete[] compressed_buffers_[i];
					compressed_buffers_[i] = NULL;
				}
				delete[] compressed_buffers_;
				compressed_buffers_ = NULL;
				delete[] compressed_buf_offsets_;
				compressed_buf_offsets_ = NULL;
#endif
				for (size_t i = 0; i < thread_count_; ++i){
					delete[] buffers_[i];
					buffers_[i] = NULL;
				}
				delete[] buffers_;
				buffers_ = NULL;
				delete[] buffer_offsets_;
				buffer_offsets_ = NULL;
				delete[] txn_offsets_;
				txn_offsets_ = NULL;
				delete[] last_epochs_;
				last_epochs_ = NULL;
			}

			void InsertRecord(const size_t &thread_id, const uint8_t &table_id, char *data, const uint8_t &data_size, const uint64_t &commit_ts) {
				char *buffer_ptr = buffers_[thread_id] + buffer_offsets_[thread_id];
				size_t &offset_ref = txn_offsets_[thread_id];
				memcpy(buffer_ptr + offset_ref, (char*)(&kInsert), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, (char*)(&table_id), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, (char*)(&data_size), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, data, data_size);
				offset_ref += data_size;
				memcpy(buffer_ptr + offset_ref, (char*)(&commit_ts), sizeof(uint64_t));
				offset_ref += sizeof(uint64_t);
			}

			void UpdateRecord(const size_t &thread_id, const uint8_t &table_id, char *data, const uint8_t &data_size, const uint64_t &commit_ts) {
				char *buffer_ptr = buffers_[thread_id] + buffer_offsets_[thread_id];
				size_t &offset_ref = txn_offsets_[thread_id];
				memcpy(buffer_ptr + offset_ref, (char*)(&kUpdate), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, (char*)(&table_id), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, (char*)(&data_size), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, data, data_size);
				offset_ref += data_size;
				memcpy(buffer_ptr + offset_ref, (char*)(&commit_ts), sizeof(uint64_t));
				offset_ref += sizeof(uint64_t);
			}

			void DeleteRecord(const size_t &thread_id, const uint8_t &table_id, const std::string &primary_key, const uint64_t &commit_ts) {
				char *buffer_ptr = buffers_[thread_id] + buffer_offsets_[thread_id];
				size_t &offset_ref = txn_offsets_[thread_id];
				memcpy(buffer_ptr + offset_ref, (char*)(&kDelete), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, (char*)(&table_id), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				size_t size = primary_key.size();
				memcpy(buffer_ptr + offset_ref, (char*)(&size), sizeof(uint8_t));
				offset_ref += sizeof(uint8_t);
				memcpy(buffer_ptr + offset_ref, primary_key.c_str(), size);
				offset_ref += size;
				memcpy(buffer_ptr + offset_ref, (char*)(&commit_ts), sizeof(uint64_t));
				offset_ref += sizeof(uint64_t);
			}

			void CommitTransaction(const size_t &thread_id, const uint64_t &epoch){
				if (epoch == -1){
					FILE *file_ptr = outfiles_[thread_id];
#if defined(COMPRESSION)
					size_t& offset = compressed_buf_offsets_[thread_id];
					size_t n = LZ4F_compressUpdate(compression_contexts_[thread_id], compressed_buffers_[thread_id] + offset, compressed_buf_size_ - offset, buffers_[thread_id], buffer_offsets_[thread_id], NULL);
					assert(LZ4F_isError(n) == false);
					offset += n;

					// after compression, write into file
					fwrite(compressed_buffers_[thread_id], sizeof(char), offset, file_ptr);
					offset = 0;
#else
					fwrite(buffers_[thread_id], sizeof(char), buffer_offsets_[thread_id], file_ptr);
#endif
					int ret;
					ret = fflush(file_ptr);
					assert(ret == 0);
#if defined(__linux__)
					ret = fsync(fileno(file_ptr));
					assert(ret == 0);
#endif
					return;

				}
				char *buffer_ptr = buffers_[thread_id] + buffer_offsets_[thread_id];
				memcpy(buffer_ptr, (char*)(&txn_offsets_[thread_id]), sizeof(size_t));
				buffer_offsets_[thread_id] += txn_offsets_[thread_id];
				assert(buffer_offsets_[thread_id] < kValueLogBufferSize);
				if (epoch != last_epochs_[thread_id]){
					FILE *file_ptr = outfiles_[thread_id];
					last_epochs_[thread_id] = epoch;
#if defined(COMPRESSION)
					size_t& offset = compressed_buf_offsets_[thread_id];
					size_t n = LZ4F_compressUpdate(compression_contexts_[thread_id], compressed_buffers_[thread_id] + offset, compressed_buf_size_ - offset, buffers_[thread_id], buffer_offsets_[thread_id], NULL);
					assert(LZ4F_isError(n) == false);
					offset += n;

					// after compression, write into file
					fwrite(compressed_buffers_[thread_id], sizeof(char), offset, file_ptr);
					offset = 0;
#else
					fwrite(buffers_[thread_id], sizeof(char), buffer_offsets_[thread_id], file_ptr);
#endif
					int ret;
					ret = fflush(file_ptr);
					assert(ret == 0);
#if defined(__linux__)
					ret = fsync(fileno(file_ptr));
					assert(ret == 0);
#endif
					buffer_offsets_[thread_id] = 0;
				}
				txn_offsets_[thread_id] = sizeof(size_t);
			}

			void AbortTransaction(const size_t &thread_id){
				txn_offsets_[thread_id] = sizeof(size_t);
			}

		private:
			AccessLogger(const AccessLogger &);
			AccessLogger& operator=(const AccessLogger &);

		private:
			char **buffers_;
			size_t *buffer_offsets_;
			size_t *txn_offsets_;
			uint64_t *last_epochs_;
#if defined(COMPRESSION)
			LZ4F_compressionContext_t* compression_contexts_;
			char **compressed_buffers_;
			size_t *compressed_buf_offsets_;
			size_t compressed_buf_size_;
#endif
		};
	}
}

#endif