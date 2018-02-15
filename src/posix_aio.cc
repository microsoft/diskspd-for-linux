// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <pthread.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <aio.h>
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "target.h"
#include "async_io.h"
#include "posix_aio.h"

namespace diskspd {

	class _PosixAsyncIop : public IAsyncIop {
		public:
			_PosixAsyncIop() {
				assert(!"Not implemented");
			}
			_PosixAsyncIop(
					Type t,
					int fd,
					off_t offset,
					void * read_buf,
					void * write_buf,
					size_t nbytes,
					int group_id,
					std::shared_ptr<TargetData> t_data,
					uint64_t time_stamp
					) {

				target_data = t_data;
				this->group_id = group_id;

				this->read_buf = read_buf;
				this->write_buf = write_buf;

				memset(&cb, 0, sizeof(cb));
				type = t;
				cb.aio_offset = offset;
				cb.aio_fildes = fd;
				cb.aio_buf = t == READ ? read_buf : write_buf;
				cb.aio_nbytes = nbytes;

				// this gets overridden in the caller if need be
				cb.aio_sigevent.sigev_notify = SIGEV_NONE;

			}
			~_PosixAsyncIop(){}

			Type get_type() { return type; }

			void set_type(Type t) {
				type = t;
				// change which buffer the cb points to
				cb.aio_buf = t == READ ? read_buf : write_buf;
			}

			int get_fd() { return cb.aio_fildes; }
			void set_fd(int fd) { cb.aio_fildes = fd; }

			off_t get_offset() { return cb.aio_offset; }
			void set_offset(off_t o) { cb.aio_offset = o; }

			size_t get_nbytes() { return cb.aio_nbytes; }
			void set_nbytes(size_t n) { cb.aio_nbytes = n; }

			int get_group_id() { return group_id; }
			void set_group_id(int id) { group_id = id; }

			uint64_t get_time() { return time; }
			void set_time(uint64_t time_stamp) { time = time_stamp; }

			std::shared_ptr<TargetData> get_target_data() { return target_data; }
			void set_target_data(std::shared_ptr<TargetData> t_data) { target_data = t_data; };

			int get_ret() { return aio_return(&cb); }
			int get_errno() { return aio_error(&cb); }

		private:
			friend _PosixAsyncIOManager;
			Type type = READ;
			int group_id = 0;
			// the control block where most stuff gets stored, used by the posix calls
			aiocb cb;
			std::shared_ptr<TargetData> target_data;
			uint64_t time;

			void * read_buf;
			void * write_buf;

	};

	// this is the class that 'privately' implements the posix aio classes
	class _PosixAsyncIOManager {

		public:

			bool start(int n_concurrent) {

				// initialize using glibc function
				aioinit init;
				init.aio_threads = n_concurrent;
				init.aio_num = n_concurrent;
				init.aio_idle_time = 1;
				d_printf("initializing aio with n_concurrent %d\n", n_concurrent);
				aio_init((const aioinit *)&init);
				started = true;
				return true;
			}

			bool create_group(int group_id, int n_concurrent) {
				if (!started) assert(!"IOManager not started!");

				// create the queue for this group
				group_queues_mutex.lock();
				if (group_queues.count(group_id)) {
					fprintf(stderr, "group already exists\n");
					group_queues_mutex.unlock();
					return false;
				}
				group_queues[group_id] = std::make_shared<std::vector<std::shared_ptr<_PosixAsyncIop>>>();
				group_queues_mutex.unlock();

				//create the vector of aiocbs for this group
				suspend_mutex.lock();
				if (suspend_vecs.count(group_id)) {
					fprintf(stderr, "group already exists\n");
					suspend_mutex.unlock();
					return false;
				}
				suspend_vecs[group_id] = std::make_shared<std::vector<aiocb*>>();
				suspend_mutex.unlock();

				return true;
			}

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
					) {
				if (!started) assert(!"IOManager not started!");

				// create the op
				auto op = std::make_shared<_PosixAsyncIop>(
						type,fd,offset,read_buf,write_buf,nbytes,group_id, t_data, time_stamp
					);

				// it gets upcast to IAsyncIop implicitly
				return op;
			}

			int enqueue(std::shared_ptr<IAsyncIop> ia) {
				if (!started) assert(!"IOManager not started!");

				// cast down
				std::shared_ptr<_PosixAsyncIop> a = std::static_pointer_cast<_PosixAsyncIop>(ia);

				// simply push the op to the relevant queue
				group_queues_mutex.lock();
				group_queues[a->group_id]->push_back(a);
				group_queues_mutex.unlock();

				return 0;
			}

			int submit(int group_id) {
				if (!started) assert(!"IOManager not started!");

				int ret;
				// get the group queue
				group_queues_mutex.lock();
				auto queue = group_queues[group_id];
				group_queues_mutex.unlock();
				// get the suspend vector
				suspend_mutex.lock();
				auto suspend_vec = suspend_vecs[group_id];
				suspend_mutex.unlock();

				while (queue->size()) {

					// consume the op from the back of the vector
					auto a = queue->back();
					queue->pop_back();

					// start op
					if (a->type == IAsyncIop::Type::READ) {
						ret = aio_read(&(a->cb));
					} else {
						ret = aio_write(&(a->cb));
					}
					if (ret) return ret;

					// push aiocb to vector
					suspend_vec->push_back(&(a->cb));

					// get a valid next flight id, add to struct, add to in flight map
					suspend_in_flight_mutex.lock();
					while(suspend_in_flight.count(next_flight_id)) ++next_flight_id;

					a->cb.aio_sigevent.sigev_value.sival_int = (int)next_flight_id;
					suspend_in_flight[next_flight_id] = a;
					++next_flight_id;
					suspend_in_flight_mutex.unlock();
				}

				return 0;
			}

			std::shared_ptr<IAsyncIop> wait(int group_id) {
				if (!started) assert(!"IOManager not started!");

				// suspending
				suspend_mutex.lock();
				auto vec = suspend_vecs[group_id];
				suspend_mutex.unlock();

				int err = aio_suspend((const aiocb * const *)&((*vec)[0]), (int)vec->size(), NULL);

				if (err) {
					perror("IOManager error! aio_suspend");
					exit(1);
				}

				// will throw error if not in map i guess
				for (auto it = vec->cbegin(); it != vec->cend(); ) {

					if(aio_error(*it) == EINPROGRESS) {
						++it;
						continue;
					}

					// get flight id
					unsigned int flight_id = (unsigned int)((*it)->aio_sigevent.sigev_value.sival_int);
					// erase cb from vector
					vec->erase(it);

					suspend_in_flight_mutex.lock();
					auto iop = suspend_in_flight[flight_id];
					// remove from the in flight list
					suspend_in_flight.erase(flight_id);
					suspend_in_flight_mutex.unlock();
					return iop;
				}

				fprintf(stderr, "IOManager error! No completed iops\n");
				exit(1);
			}

		private:

			bool started = false;

			// group id queues for enqueue()ing requests
			std::map<int, std::shared_ptr<std::vector<std::shared_ptr<_PosixAsyncIop>>>> group_queues;
			std::mutex group_queues_mutex;

			// group_id to vector of aiocbs
			std::map<int, std::shared_ptr<std::vector<aiocb*>>> suspend_vecs;
			std::mutex suspend_mutex;
			// flight id to asynciop
			std::map<volatile unsigned int, std::shared_ptr<_PosixAsyncIop>> suspend_in_flight;
			volatile unsigned int next_flight_id = 0;
			std::mutex suspend_in_flight_mutex;
	};

	PosixSuspendAsyncIOManager::PosixSuspendAsyncIOManager() { p = new _PosixAsyncIOManager(); }
	PosixSuspendAsyncIOManager::~PosixSuspendAsyncIOManager() { delete p; }

	bool PosixSuspendAsyncIOManager::start(int n_concurrent) { return p->start(n_concurrent); }

	bool PosixSuspendAsyncIOManager::create_group(int group_id, int n_concurrent) {
		return p->create_group(group_id, n_concurrent);
	}

	std::shared_ptr<IAsyncIop> PosixSuspendAsyncIOManager::construct(
			IAsyncIop::Type type,
			int fd,
			off_t offset,
			void* read_buf,
			void* write_buf,
			size_t nbytes,
			int group_id,
			std::shared_ptr<TargetData> t_data,
			uint64_t time_stamp
			) {
		return p->construct(type, fd, offset, read_buf, write_buf, nbytes, group_id, t_data, time_stamp);
	}

	int PosixSuspendAsyncIOManager::enqueue(std::shared_ptr<IAsyncIop> a) {
		return p->enqueue(a);
	}

	int PosixSuspendAsyncIOManager::submit(int group_id) {
		return p->submit(group_id);
	}

	std::shared_ptr<IAsyncIop> PosixSuspendAsyncIOManager::wait(int group_id) {
		return p->wait(group_id);
	}
} // namespace diskspd
