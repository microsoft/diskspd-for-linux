// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <pthread.h>
#include <vector>
#include <cstdint>
#include <memory>

#include "async_io.h"

#ifndef DISKSPD_KERNEL_AIO_H
#define DISKSPD_KERNEL_AIO_H

namespace diskspd {

	class _KernelAsyncIOManager;

	/**
	 *	Concrete IAsyncIOManager, including an implementation of IAsyncIop
	 *	This implementation uses POSIX aio, using a separate thread to handle signals
	 *	Worker threads calling wait() sleep until a request finishes
	 */
	class KernelAsyncIOManager : public IAsyncIOManager {
		public:
			KernelAsyncIOManager();
			~KernelAsyncIOManager();

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

			int enqueue(std::shared_ptr<IAsyncIop> a);

			int submit(int group_id);

			std::shared_ptr<IAsyncIop> wait(int group_id);

		private:
			// This class uses a private implementation pattern
			// We use a private class to do the actual work
			_KernelAsyncIOManager * p;
			// hence we need to disable the copy constructors
			KernelAsyncIOManager(const KernelAsyncIOManager &p);
			KernelAsyncIOManager &operator=(const KernelAsyncIOManager &p);

	};
} // namespace diskspd

#endif // DISKSPD_KERNEL_AIO_H
