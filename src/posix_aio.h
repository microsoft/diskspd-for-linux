// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <pthread.h>
#include <vector>
#include <cstdint>
#include <memory>

#include "async_io.h"

#ifndef DISKSPD_POSIX_IO_H
#define DISKSPD_POSIX_IO_H

namespace diskspd {

	class _PosixAsyncIOManager;
	/**
	 *	Concrete IAsyncIOManager, including an implementation of IAsyncIop
	 *	This implementation uses POSIX aio, polling on the results of operations (high cpu usage)
	 *	Worker threads calling wait() poll until a request finishes
	 */
	class PosixSuspendAsyncIOManager : public IAsyncIOManager {
		public:
			PosixSuspendAsyncIOManager();
			~PosixSuspendAsyncIOManager();

			bool start(int n_concurrent);

			bool create_group(int group_id, int n_concurrent);

			std::shared_ptr<IAsyncIop> construct(
					IAsyncIop::Type type,
					int fd,
					off_t offset,
					void* read_buf,
					void* write_buf,
					size_t nbytes,
					int group_id,
					std::shared_ptr<TargetData> t_data,
					uint64_t time_stamp
					);

			std::shared_ptr<IAsyncIop> construct(std::shared_ptr<IAsyncIop> a);

			int enqueue(std::shared_ptr<IAsyncIop> a);

			int submit(int group_id);

			std::shared_ptr<IAsyncIop> wait(int group_id);

		private:
			// This class uses a private implementation pattern
			// We use a private class to do the actual work
			_PosixAsyncIOManager * p;
			// hence we need to disable the copy constructors
			PosixSuspendAsyncIOManager(const PosixSuspendAsyncIOManager &p);
			PosixSuspendAsyncIOManager &operator=(const PosixSuspendAsyncIOManager &p);

	};
} // namespace diskspd
#endif // DISKSPD_POSIX_IO_H
