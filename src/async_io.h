// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <pthread.h>
#include <vector>
#include <cstdint>
#include <memory>

#include "target.h"

#ifndef DISKSPD_ASYNC_IO_H
#define DISKSPD_ASYNC_IO_H

namespace diskspd {

	/**
	 *	Abstract representation of an iop, used by IAsyncIOManager
	 */
	class IAsyncIop {
		public:
			IAsyncIop(){}
			virtual ~IAsyncIop(){}

			enum Type {
				READ,
				WRITE
			};

			virtual Type get_type()=0;
			virtual void set_type(Type t)=0;

			virtual int get_fd()=0;
			virtual void set_fd(int fd)=0;

			virtual off_t get_offset()=0;
			virtual void set_offset(off_t o)=0;

			virtual size_t get_nbytes()=0;
			virtual void set_nbytes(size_t n)=0;

			virtual int get_group_id()=0;
			virtual void set_group_id(int id)=0;

			virtual uint64_t get_time()=0;
			virtual void set_time(uint64_t time)=0;

			virtual std::shared_ptr<TargetData> get_target_data()=0;
			virtual void set_target_data(std::shared_ptr<TargetData> t_data)=0;

			// these functions return errno and return values from the underlying implementation
			// only meaningful for completed iops
			// of the IAsyncIOManager
			virtual int get_ret()=0;
			virtual int get_errno()=0;
	};

	/**
	 *	Abstract factory for creating and managing async io requests
	 *	Can also be used for synchronous io (just use it the same way; enqueue will block on I/O)
	 *	NOTE - currently assumes each group_id is used by only a single thread - i.e. using
	 *	a group_id across multiple threads is NOT safe!
	 */
	class IAsyncIOManager {
		public:
			IAsyncIOManager(){}
			virtual ~IAsyncIOManager(){}
			/**
			 *	Initialize the manager, which can handle at most n_concurrent requests
			 *	This should only be called ONCE per instance of the manager
			 *	This must be done from the main (Job) thread!
			 *	On failure, return false.
			 */
			virtual bool start(int n_concurrent)=0;

			/**
			 *	Create a group for io requests - allows a single IOManager to manage multiple
			 *	groups of io requests. This group is able to handle n_concurrent requests
			 *	This function returns false if an existing group id is used
			 */
			virtual bool create_group(int group_id, int n_concurrent)=0;

			/**
			 *	Create a fresh iop structure to pass into enqueue
			 *	Must be an existing group id
			 */
			virtual std::shared_ptr<IAsyncIop> construct(
					IAsyncIop::Type type,
					int fd,
					off_t offset,
					void* read_buf,
					void* write_buf,
					size_t nbytes,
					int group_id,
					std::shared_ptr<TargetData> t_data,
					uint64_t time_stamp
					)=0;

			/**
			 *	Enqueue an iop to be submit()ted later
			 */
			virtual int enqueue(std::shared_ptr<IAsyncIop>)=0;

			/**
			 *	Submit all enqueue()d iops
			 *	Normally doesn't block unless the implementation is synchronous
			 */
			virtual int submit(int group_id)=0;

			/**
			 *	Returns a completed AsyncIop which has the supplied group_id
			 *	Blocks until a request completes and returns it
			 *	The returned AsyncIop can be re-enqueue()d if desired, or just discarded
			 */
			virtual std::shared_ptr<IAsyncIop> wait(int group_id)=0;
	};

} // namespace diskspd

#endif // DISKSPD_ASYNC_IO_H
